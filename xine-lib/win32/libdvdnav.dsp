# Microsoft Developer Studio Project File - Name="libdvdnav" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Static Library" 0x0104

CFG=libdvdnav - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "libdvdnav.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "libdvdnav.mak" CFG="libdvdnav - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "libdvdnav - Win32 Release" (based on "Win32 (x86) Static Library")
!MESSAGE "libdvdnav - Win32 Debug" (based on "Win32 (x86) Static Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "libdvdnav - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Target_Dir ""
LINK32=link.exe
# ADD BASE LINK32 /machine:IX86
# ADD LINK32 /machine:IX86
MTL=midl.exe
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "../lib" /I "../include" /I "include" /I "include/msvc" /I "../src/xine-utils" /I "../src/input/libdvdcss/src" /I "../src" /D "WIN32" /D "NDEBUG" /D "_LIB" /D "DVDNAV_COMPILE" /D "HAVE_CONFIG_H" /YX /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo /out:"Release\libdvdnav\libdvdnav.lib"

!ELSEIF  "$(CFG)" == "libdvdnav - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
LINK32=link.exe
# ADD BASE LINK32 /machine:IX86
# ADD LINK32 /debug /machine:IX86 /out:"Debug/libdvdnav.lib" /implib:"Debug/libdvdnav.lib"
# SUBTRACT LINK32 /pdb:none /nodefaultlib
MTL=midl.exe
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "../lib" /I "../include" /I "include" /I "include/msvc" /I "../src/xine-utils" /I "../src/input/libdvdcss/src" /I "../src" /D "WIN32" /D "_DEBUG" /D "_LIB" /D "DVDNAV_COMPILE" /D "HAVE_CONFIG_H" /FR"Debug/libdvdnav/" /Fp"Debug/libdvdnav/libdvdnav.pch" /YX /Fo"Debug/libdvdnav/" /Fd"Debug/libdvdnav/" /FD /GZ /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo /out:"Debug\libdvdnav\libdvdnav.lib"

!ENDIF 

# Begin Target

# Name "libdvdnav - Win32 Release"
# Name "libdvdnav - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\src\input\libdvdnav\decoder.c
# End Source File
# Begin Source File

SOURCE=..\src\input\libdvdnav\dvd_input.c
# End Source File
# Begin Source File

SOURCE=..\src\input\libdvdnav\dvd_reader.c
# End Source File
# Begin Source File

SOURCE=..\src\input\libdvdnav\dvd_udf.c
# End Source File
# Begin Source File

SOURCE=..\src\input\libdvdnav\dvdnav.c
# End Source File
# Begin Source File

SOURCE=..\src\input\libdvdnav\highlight.c
# End Source File
# Begin Source File

SOURCE=..\src\input\libdvdnav\ifo_read.c
# End Source File
# Begin Source File

SOURCE=..\src\input\libdvdnav\md5.c
# End Source File
# Begin Source File

SOURCE=..\src\input\libdvdnav\nav_print.c
# End Source File
# Begin Source File

SOURCE=..\src\input\libdvdnav\nav_read.c
# End Source File
# Begin Source File

SOURCE=..\src\input\libdvdnav\navigation.c
# End Source File
# Begin Source File

SOURCE=..\src\input\libdvdnav\read_cache.c
# End Source File
# Begin Source File

SOURCE=..\src\input\libdvdnav\remap.c
# End Source File
# Begin Source File

SOURCE=..\src\input\libdvdnav\searching.c
# End Source File
# Begin Source File

SOURCE=..\src\input\libdvdnav\settings.c
# End Source File
# Begin Source File

SOURCE=..\src\input\libdvdnav\vm.c
# End Source File
# Begin Source File

SOURCE=..\src\input\libdvdnav\vmcmd.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\dvdread\bswap.h
# End Source File
# Begin Source File

SOURCE=..\dvdread\dvd_input.h
# End Source File
# Begin Source File

SOURCE=..\dvdread\dvd_reader.h
# End Source File
# Begin Source File

SOURCE=..\dvdread\dvd_udf.h
# End Source File
# Begin Source File

SOURCE=..\dvdread\ifo_print.h
# End Source File
# Begin Source File

SOURCE=..\dvdread\ifo_read.h
# End Source File
# Begin Source File

SOURCE=..\dvdread\ifo_types.h
# End Source File
# Begin Source File

SOURCE=..\dvdread\nav_print.h
# End Source File
# Begin Source File

SOURCE=..\dvdread\nav_read.h
# End Source File
# Begin Source File

SOURCE=..\dvdread\nav_types.h
# End Source File
# Begin Source File

SOURCE=..\dvdread\types.h
# End Source File
# End Group
# Begin Group "DLL Defs"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\libdvdnav.def
# End Source File
# End Group
# End Target
# End Project
