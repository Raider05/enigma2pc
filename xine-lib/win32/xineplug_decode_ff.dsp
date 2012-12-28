# Microsoft Developer Studio Project File - Name="xineplug_decode_ff" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=xineplug_decode_ff - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "xineplug_decode_ff.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "xineplug_decode_ff.mak" CFG="xineplug_decode_ff - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "xineplug_decode_ff - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "xineplug_decode_ff - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "xineplug_decode_ff - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release/xineplug_decode_ff"
# PROP Intermediate_Dir "Release/xineplug_decode_ff"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
LIB32=link.exe
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "XINEPLUG_DECODE_FF_EXPORTS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "../lib" /I "../include" /I "include" /I "include/msvc" /I "../src/xine-engine" /I "../src" /I "../src/xine-utils" /I "../../ffmpeg/include/ffmpeg" /I "../../ffmpeg/include/postproc" /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "HAVE_CONFIG_H" /D "HAVE_FFMPEG" /D "XINEPLUG_DECODE_FF_EXPORTS" /D "XINE_COMPILE" /D "SIMPLE_IDCT" /D "RUNTIME_CPUDETECT" /D "USE_FASTMEMCPY" /D "CONFIG_RISKY" /D "CONFIG_DECODERS" /D "XINE_MPEG_ENCODER" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 avcodec.lib pthread.lib /nologo /dll /machine:I386 /out:"Release/lib/xine/plugins/xineplug_decode_ff.dll" /libpath:"Release" /libpath:"../../ffmpeg/"

!ELSEIF  "$(CFG)" == "xineplug_decode_ff - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug/xineplug_decode_ff"
# PROP Intermediate_Dir "Debug/xineplug_decode_ff"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
LIB32=link.exe
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "XINEPLUG_DECODE_FF_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "../lib" /I "../include" /I "include" /I "include/msvc" /I "../src/xine-engine" /I "../src" /I "../src/xine-utils" /I "../../ffmpeg/include/ffmpeg" /I "../../ffmpeg/include/postproc" /D "_DEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "HAVE_CONFIG_H" /D "HAVE_FFMPEG" /D "XINEPLUG_DECODE_FF_EXPORTS" /D "XINE_COMPILE" /D "SIMPLE_IDCT" /D "RUNTIME_CPUDETECT" /D "USE_FASTMEMCPY" /D "CONFIG_RISKY" /D "CONFIG_DECODERS" /D "XINE_MPEG_ENCODER" /FD /GZ /c
# SUBTRACT CPP /X /YX
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 avcodec.lib pthread.lib /nologo /dll /debug /machine:I386 /out:"Debug/lib/xine/plugins/xineplug_decode_ff.dll" /pdbtype:sept /libpath:"Debug" /libpath:"../../ffmpeg/"

!ENDIF 

# Begin Target

# Name "xineplug_decode_ff - Win32 Release"
# Name "xineplug_decode_ff - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\src\libffmpeg\audio_decoder.c
# End Source File
# Begin Source File

SOURCE=..\src\libffmpeg\mpeg_parser.c
# End Source File
# Begin Source File

SOURCE=..\src\libffmpeg\video_decoder.c
# End Source File
# Begin Source File

SOURCE=..\src\libffmpeg\xine_decoder.c
# End Source File
# End Group
# Begin Group "DLL Defs"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\xine_plugin.def
# End Source File
# End Group
# End Target
# End Project
