@echo off
chcp 65001 >nul
cd /d "%~dp0"
python tools\workbench_server.py --open
if errorlevel 1 (
    echo.
    echo 启动失败：请先构建项目并安装 Python 3.10 或更高版本。
    echo 查看 docs\workbench.md 获取启动方法。
    pause
)
