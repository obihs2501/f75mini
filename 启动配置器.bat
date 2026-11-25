@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo ============================================
echo   AULA F75 配置器
echo ============================================
echo.
echo 正在启动本地服务...
echo 请在 Chrome / Edge 中打开:
echo.
echo        http://localhost:8765/f75-studio.html
echo.
echo 关闭此窗口即停止服务。
echo ============================================
echo.
python -m http.server 8765 --bind 127.0.0.1
pause
