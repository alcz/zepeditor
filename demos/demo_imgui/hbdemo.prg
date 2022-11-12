/*
    hbdemo.prg    -- ZEP Editor integration example

    license is MIT, see ../LICENSE
*/

#include "hbimenum.ch"

REQUEST HB_CODEPAGE_PL852
REQUEST HB_CODEPAGE_EL737
REQUEST HB_CODEPAGE_UTF8EX

PROCEDURE MAIN

   hb_cdpSelect("UTF8EX")

#ifndef __PLATFORM__WEB
   IF ! File( "OpenSans-Regular.ttf" )
      Alert("can't find my font")
   ENDIF
#endif
   hb_sokol_imguiNoDefaultFont( .T. )
   sapp_run_default( "Custom Font Example", 800, 600 )

#ifdef __PLATFORM__WEB
   IF ImFrame() # NIL /* dummy calls for emscripten, to be removed when those functions are properly requested from .c code */
      ImInit()
   ENDIF
#endif
   RETURN

PROCEDURE ImInit
#ifdef __PLATFORM__WEB
   LOCAL cFontBuf
#pragma __binarystreaminclude "OpenSans-Regular.ttf"|cFontBuf := %s
   hb_igAddFontFromMemoryTTF( cFontBuf, 18.0, , { "EL737", "PL852" }, .T., .F. )
#else
   hb_igAddFontFromFileTTF( "OpenSans-Regular.ttf", 18.0, , { "EL737", "PL852" }, .T., .F. )
#endif

   ZEP_INIT()

   hb_sokol_imguiFont2Texture()

#ifdef ImGuiConfigFlags_DockingEnable
   hb_igConfigFlagsAdd( ImGuiConfigFlags_DockingEnable )
#endif

   RETURN

PROCEDURE ImFrame
   STATIC counter := 0, s, c, d

#ifdef ImGuiConfigFlags_DockingEnable
   igDockSpaceOverViewPort()
#endif

   ZEP_LOOP()
   
   RETURN
