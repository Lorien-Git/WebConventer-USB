@echo off
title WebConventer-USB - Quick Git Push
chcp 65001 >nul
cd /d "%~dp0"

echo ======================================================
echo    WebConventer-USB - GitHub Publisher
echo ======================================================
echo.

:: Check if git is installed
where git >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Git is not installed or not in PATH!
    echo Please install Git from https://git-scm.com/
    echo.
    pause
    exit /b 1
)

:: Initialize git repo if not present
if not exist ".git" (
    echo [1/5] Initializing Git repository...
    git init
    git branch -M main
) else (
    echo [1/5] Git repository already initialized.
)

:: Set remote origin
echo [2/5] Setting remote origin (https://github.com/Lorien-Git/WebConventer-USB.git)...
git remote remove origin >nul 2>&1
git remote add origin https://github.com/Lorien-Git/WebConventer-USB.git

:: Pack latest resources and stage files
echo [3/5] Staging files for commit...
git add .

:: Commit
echo [4/5] Creating commit...
git commit -m "Initial Release: WebConventer-USB v1.0.0"

:: Push
echo [5/5] Pushing to GitHub (main branch)...
echo.
git push -u origin main

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ======================================================
    echo    SUCCESS! Repository pushed to GitHub:
    echo    https://github.com/Lorien-Git/WebConventer-USB
    echo ======================================================
) else (
    echo.
    echo [WARNING] Push encountered an issue. If this is a new repo, check your GitHub credentials.
)

echo.
pause
