#include "hbapi.h"
#include "hbapierr.h"
#include "hbapiitm.h"

#include <stdio.h>
#include <thread>

// this is included by "zep/imgui/..."
// #include "imgui/imgui.h"

#include "zep/mcommon/animation/timer.h"

#undef max

#include "zep/filesystem.h"
#include "zep/imgui/display_imgui.h"
#include "zep/imgui/editor_imgui.h"
#include "zep/mode_standard.h"
#include "zep/mode_vim.h"
#include "zep/tab_window.h"
#include "zep/theme.h"
#include "zep/window.h"

#include "zep/regress.h"

#include "repl/mode_repl.h"

#include "mutils/ui/dpi.h"

#include "sokol_app.h"

using namespace Zep;
using namespace MUtils;

namespace
{

const float DemoFontPtSize = 14.0f;

const std::string shader = R"R(
#version 330 core

uniform mat4 Projection;

// Coordinates  of the geometry
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_tex_coord;
layout(location = 2) in vec4 in_color;

// Outputs to the pixel shader
out vec2 frag_tex_coord;
out vec4 frag_color;

void main()
{
    gl_Position = Projection * vec4(in_position.xyz, 1.0);
    frag_tex_coord = in_tex_coord;
    frag_color = in_color;
}
)R";

Zep::NVec2f GetPixelScale()
{
   return Zep::NVec2f( sapp_dpi_scale(), sapp_dpi_scale() );
}

}

std::string startupFile;

// A helper struct to init the editor and handle callbacks
struct ZepContainerImGui : public IZepComponent, public IZepReplProvider
{
    ZepContainerImGui(const std::string& startupFilePath, const std::string& configPath)
        : spEditor(std::make_unique<ZepEditor_ImGui>(configPath, GetPixelScale()))
        //, fileWatcher(spEditor->GetFileSystem().GetConfigPath(), std::chrono::seconds(2))
    {

        // ZepEditor_ImGui will have created the fonts for us; but we need to build
        // the font atlas
        auto fontPath = std::string( /* SDL_GetBasePath()) + */ "Cousine-Regular.ttf" );
        auto& display = static_cast<ZepDisplay_ImGui&>(spEditor->GetDisplay());

        int fontPixelHeight = (int)dpi_pixel_height_from_point_size(DemoFontPtSize, GetPixelScale().y);

        auto& io = ImGui::GetIO();
        ImVector<ImWchar> ranges;
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault()); // Add one of the default ranges
        builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic()); // Add one of the default ranges
        builder.AddRanges(greek_range);
        builder.BuildRanges(&ranges); // Build the final result (ordered ranges with all the unique characters submitted)

        ImFontConfig cfg;
        cfg.OversampleH = 4;
        cfg.OversampleV = 4;

        auto pImFont = ImGui::GetIO().Fonts->AddFontFromFileTTF(fontPath.c_str(), float(fontPixelHeight), &cfg, ranges.Data);

        display.SetFont(ZepTextType::UI, std::make_shared<ZepFont_ImGui>(display, pImFont, fontPixelHeight));
        display.SetFont(ZepTextType::Text, std::make_shared<ZepFont_ImGui>(display, pImFont, fontPixelHeight));
        display.SetFont(ZepTextType::Heading1, std::make_shared<ZepFont_ImGui>(display, pImFont, int(fontPixelHeight * 1.75)));
        display.SetFont(ZepTextType::Heading2, std::make_shared<ZepFont_ImGui>(display, pImFont, int(fontPixelHeight * 1.5)));
        display.SetFont(ZepTextType::Heading3, std::make_shared<ZepFont_ImGui>(display, pImFont, int(fontPixelHeight * 1.25)));

//      handled outside
//        unsigned int flags = 0; // ImGuiFreeType::NoHinting;
//        ImGui::BuildFontAtlas(ImGui::GetIO().Fonts, flags);

        spEditor->RegisterCallback(this);

/*      

        ZepMode_Orca::Register(*spEditor);

        ZepRegressExCommand::Register(*spEditor);

        // Repl
        ZepReplExCommand::Register(*spEditor, this);
        ZepReplEvaluateOuterCommand::Register(*spEditor, this);
        ZepReplEvaluateInnerCommand::Register(*spEditor, this);
        ZepReplEvaluateCommand::Register(*spEditor, this);
*/

        if (!startupFilePath.empty())
        {
            spEditor->InitWithFileOrDir(startupFilePath);
        }
        else
        {
            spEditor->InitWithText("Shader.vert", shader);
        }

        // File watcher not used on apple yet ; needs investigating as to why it doesn't compile/run
        // The watcher is being used currently to update the config path, but clients may want to do more interesting things
        // by setting up watches for the current dir, etc.
        /*fileWatcher.start([=](std::string path, FileStatus status) {
            if (spEditor)
            {
                ZLOG(DBG, "Config File Change: " << path);
                spEditor->OnFileChanged(spEditor->GetFileSystem().GetConfigPath() / path);
            }
        });*/
    }

    ~ZepContainerImGui()
    {
    }

    void Destroy()
    {
        spEditor->UnRegisterCallback(this);
        spEditor.reset();
    }

    // Inherited via IZepComponent
    virtual void Notify(std::shared_ptr<ZepMessage> message) override
    {
        if (message->messageId == Msg::GetClipBoard)
        {
            message->str = sapp_get_clipboard_string();
            message->handled = true;
        }
        else if (message->messageId == Msg::SetClipBoard)
        {
            sapp_set_clipboard_string(message->str.c_str());
            message->handled = true;
        }
        else if (message->messageId == Msg::RequestQuit)
        {
            quit = true;
        }
        else if (message->messageId == Msg::ToolTip)
        {
            auto spTipMsg = std::static_pointer_cast<ToolTipMessage>(message);
            if (spTipMsg->location.Valid() && spTipMsg->pBuffer)
            {
                auto pSyntax = spTipMsg->pBuffer->GetSyntax();
                if (pSyntax)
                {
                    if (pSyntax->GetSyntaxAt(spTipMsg->location).foreground == ThemeColor::Identifier)
                    {
                        auto spMarker = std::make_shared<RangeMarker>(*spTipMsg->pBuffer);
                        spMarker->SetDescription("This is an identifier");
                        spMarker->SetHighlightColor(ThemeColor::Identifier);
                        spMarker->SetTextColor(ThemeColor::Text);
                        spTipMsg->spMarker = spMarker;
                        spTipMsg->handled = true;
                    }
                    else if (pSyntax->GetSyntaxAt(spTipMsg->location).foreground == ThemeColor::Keyword)
                    {
                        auto spMarker = std::make_shared<RangeMarker>(*spTipMsg->pBuffer);
                        spMarker->SetDescription("This is a keyword");
                        spMarker->SetHighlightColor(ThemeColor::Keyword);
                        spMarker->SetTextColor(ThemeColor::Text);
                        spTipMsg->spMarker = spMarker;
                        spTipMsg->handled = true;
                    }
                }
            }
        }
    }

    virtual ZepEditor& GetEditor() const override
    {
        return *spEditor;
    }

    bool quit = false;
    std::unique_ptr<ZepEditor_ImGui> spEditor;
    //FileWatcher fileWatcher;
};




/*
Zep::NVec2f GetPixelScale()
{
    float ddpi = 0.0f;
    float hdpi = 0.0f;
    float vdpi = 0.0f;

    auto window = SDL_GL_GetCurrentWindow();
    auto index = window ? SDL_GetWindowDisplayIndex(window) : 0;

    auto res = SDL_GetDisplayDPI(index, &ddpi, &hdpi, &vdpi);
    if (res == 0 && hdpi != 0)
    {
        return Zep::NVec2f(hdpi, vdpi) / 96.0f;
    }
    return Zep::NVec2f(1.0f);
}
*/

ZepContainerImGui* s_zep;

HB_FUNC( ZEP_INIT )
{
   // ** Zep specific code, before Initializing font map
   // ZepContainerImGui zep(startupFile, SDL_GetBasePath());
   // hacky static initialization... 
   static ZepContainerImGui zep( "", "" );
   s_zep = &zep;

   // Setup style
   ImGui::StyleColorsDark();
}

   // ZLOG(INFO, "DPI Scale: " << MUtils::NVec2f(GetPixelScale().x, GetPixelScale().y));

HB_FUNC( ZEP_LOOP )
{
   static bool show_demo_window = false;

            // Save battery by skipping display if not required.
            // This will check for cursor flash, for example, to keep that updated.
/*            if (!zep.spEditor->RefreshRequired())
            {
                continue;
            }
*/
 
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open"))
                {
/*
                    auto openFileName = tinyfd_openFileDialog(
                        "Choose a file",
                        "",
                        0,
                        nullptr,
                        nullptr,
                        0);
                    if (openFileName != nullptr)
                    {
                        auto pBuffer = s_zep->GetEditor().GetFileBuffer(openFileName);
                        if (pBuffer)
                        {
                            s_zep->GetEditor().EnsureWindow(*pBuffer);
                        }
                    }
*/
                }
                ImGui::EndMenu();
            }

            const auto pBuffer = s_zep->GetEditor().GetActiveBuffer();

            if (ImGui::BeginMenu("Settings"))
            {
                if (ImGui::BeginMenu("Editor Mode", pBuffer))
                {
                    bool enabledVim = strcmp(pBuffer->GetMode()->Name(), Zep::ZepMode_Vim::StaticName()) == 0;
                    bool enabledNormal = !enabledVim;
                    if (ImGui::MenuItem("Vim", "CTRL+2", &enabledVim))
                    {
                        s_zep->GetEditor().SetGlobalMode(Zep::ZepMode_Vim::StaticName());
                    }
                    else if (ImGui::MenuItem("Standard", "CTRL+1", &enabledNormal))
                    {
                        s_zep->GetEditor().SetGlobalMode(Zep::ZepMode_Standard::StaticName());
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Theme"))
                {
                    bool enabledDark = s_zep->GetEditor().GetTheme().GetThemeType() == ThemeType::Dark ? true : false;
                    bool enabledLight = !enabledDark;

                    if (ImGui::MenuItem("Dark", "", &enabledDark))
                    {
                        s_zep->GetEditor().GetTheme().SetThemeType(ThemeType::Dark);
                    }
                    else if (ImGui::MenuItem("Light", "", &enabledLight))
                    {
                        s_zep->GetEditor().GetTheme().SetThemeType(ThemeType::Light);
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Window"))
            {
                auto pTabWindow = s_zep->GetEditor().GetActiveTabWindow();
                bool selected = false;
                if (ImGui::MenuItem("Horizontal Split", "", &selected, pBuffer != nullptr))
                {
                    pTabWindow->AddWindow(pBuffer, pTabWindow->GetActiveWindow(), RegionLayoutType::VBox);
                }
                else if (ImGui::MenuItem("Vertical Split", "", &selected, pBuffer != nullptr))
                {
                    pTabWindow->AddWindow(pBuffer, pTabWindow->GetActiveWindow(), RegionLayoutType::HBox);
                }
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        int w = sapp_width(), h = sapp_height();

        // This is a bit messy; and I have no idea why I don't need to remove the menu fixed_size from the calculation!
        // The point of this code is to fill the main window with the Zep window
        // It is only done once so the user can play with the window if they want to for testing
        auto menuSize = ImGui::GetStyle().FramePadding.y * 2 + ImGui::GetFontSize();
        ImGui::SetNextWindowPos(ImVec2(0, menuSize), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(float(w), float(h - menuSize)), ImGuiCond_Always);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::Begin("Zep", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);

        auto min = ImGui::GetCursorScreenPos();
        auto max = ImGui::GetContentRegionAvail();
        max.x = std::max(1.0f, max.x);
        max.y = std::max(1.0f, max.y);

        // Fill the window
        max.x = min.x + max.x;
        max.y = min.y + max.y;
        s_zep->spEditor->SetDisplayRegion(Zep::NVec2f(min.x, min.y), Zep::NVec2f(max.x, max.y));

        // Display the editor inside this window
        s_zep->spEditor->Display();
        s_zep->spEditor->HandleInput();

        ImGui::End();
        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor(1);

        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);


//    s_zep->Destroy();

    return;
}



