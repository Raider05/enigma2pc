ECHO copying fonts to %1\share\xine\libxine1\fonts ...
mkdir %1\share
mkdir %1\share\xine
mkdir %1\share\xine\libxine1
mkdir %1\share\xine\libxine1\fonts
xcopy /Y /s ..\misc\fonts\*.gz %1\share\xine\libxine1\fonts

ECHO copying headers to %1\include\xine
mkdir %1\include
mkdir %1\include\xine
xcopy /Y /s ..\include\xine.h %1\include
xcopy /Y /s ..\lib\os_types.h %1\include\xine
xcopy /Y /s ..\src\xine-engine\*.h %1\include\xine
xcopy /Y /s ..\src\xine-utils\*.h %1\include\xine
xcopy /Y /s ..\src\demuxers\demux.h %1\include\xine
xcopy /Y /s ..\src\input\input_plugin.h %1\include\xine
del %1\include\xine\accel_xvmc.h
del %1\include\xine\bswap.h
del %1\include\xine\lrb.h
del %1\include\xine\ppcasm_string.h
del %1\include\xine\xine_check.h
