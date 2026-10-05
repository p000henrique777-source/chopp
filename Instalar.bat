@echo off
chcp 65001 >nul
net session >nul 2>&1
if %errorlevel% neq 0 (
  echo Pedindo permissao de administrador...
  powershell -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)
echo Instalando o ChopEasy...
xcopy /E /I /Y "%~dp0ChopEasy.vst3" "%CommonProgramFiles%\VST3\ChopEasy.vst3"
if errorlevel 1 (
  echo.
  echo ERRO ao copiar. Copie a pasta ChopEasy.vst3 manualmente para: %CommonProgramFiles%\VST3
  pause
  exit /b 1
)
echo.
echo PRONTO! Abra o FL Studio e va em Options ^> Manage plugins ^> Find plugins.
pause
