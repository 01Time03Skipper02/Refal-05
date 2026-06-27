@echo off
setlocal
  set MODULES=refal05c R05-CompilerUtils R05-Generator R05-Parser
  set LIBS=LibraryEx R5FW-Parser-Defs R5FW-Parser R5FW-Plainer
  set LIBS=%LIBS% R5FW-Transformer Platform

  md ..\bin 2>NUL

  if {%1}=={stable} (
    refc %MODULES%
    move *.rsl ..\bin >NUL
    set EXECUTABLE=refgo -l20 ../bin^(%MODULES: =+%^)+%LIBS: =+%
  ) else (
    set EXECUTABLE=..\bin\refal05c.exe
  )
  
  if {%1}=={lambda} (
    md __rlmake_tmp 2>NUL
    call rlmake --debug --tmp-dir __rlmake_tmp --dont-keep-rasl -o..\bin\refal05c.exe --ref5rsl refal05c.ref
    echo.
  )

  if {%2}=={and_stop} goto :EOF

  call ..\c-plus-plus.conf.bat
  set R05CFLAGS=-orefal05c %R05CFLAGS%
  set R05PATH=..\lib
  for %%F in (%MODULES% %LIBS%) do if exist "%%F.c" erase "%%F.c"
  echo Self-applying Refal-05 compiler...
  echo Y|%EXECUTABLE% %MODULES% Library refal05rts ^
    cl_iter_table.c ^
    compact_iter.c ^
    compact_list.c ^
    compact_runtime_storage.c ^
    %LIBS%
  if errorlevel 1 exit /b 1
  if exist a.exe move /Y a.exe refal05c.exe >NUL
  if exist *.obj erase *.obj
  if exist *.tds erase *.tds

  if not exist refal05c.exe (
    echo SELF-APPLICATION FAILED: refal05c.exe was not generated
    exit /b 1
  )
  if exist refal05c.exe move /Y refal05c.exe ..\bin >NUL
  if errorlevel 1 exit /b 1

  md cfiles 2>NUL
  if exist *.c move /Y *.c cfiles >NUL
endlocal
