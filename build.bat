@echo off
chcp 65001 >nul
echo ============================================
echo  ChopEasy - compilando (a primeira vez demora)
echo ============================================

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 goto erro

cmake --build build --config Release --target ChopEasy_VST3 --parallel 2
if errorlevel 1 goto erro

echo.
echo Copiando para a pasta de plugins VST3...
xcopy /E /I /Y "build\ChopEasy_artefacts\Release\VST3\ChopEasy.vst3" "%CommonProgramFiles%\VST3\ChopEasy.vst3"
if errorlevel 1 (
  echo.
  echo Nao consegui copiar. Clique com o botao direito no build.bat e use "Executar como administrador",
  echo ou copie a pasta ChopEasy.vst3 manualmente para: %CommonProgramFiles%\VST3
  goto fim
)

echo.
echo PRONTO! Abra o FL Studio e faca "Find plugins" (veja o LEIAME.txt).
goto fim

:erro
echo.
echo DEU ERRO. Copie a mensagem de erro acima e me mande que eu corrijo.

:fim
pause
