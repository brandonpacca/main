@echo off
rem Registra madcatz_wheel.exe per l'avvio automatico all'accesso di Windows (utente corrente).
rem Per rimuoverlo:  schtasks /Delete /TN "MadCatzWheelBridge" /F
schtasks /Create /TN "MadCatzWheelBridge" /TR "\"%~dp0madcatz_wheel.exe\"" /SC ONLOGON /RL LIMITED /F
if errorlevel 1 (echo Errore nella creazione dell'attivita'.) else (echo Avvio automatico configurato.)
pause
