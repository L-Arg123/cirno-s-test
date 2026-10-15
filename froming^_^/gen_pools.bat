@echo off
cd /d "%~dp0"
echo 自动生成 7 个档位题库（normal - hard - veryhard - hardcore - extreme - insane - torment）
echo 20/24/26 档较慢，请耐心等待，不要关窗口。
nines.exe -top 60 -max 3000
echo 完成。看 normal.csv / hard.csv / veryhard.csv / hardcore.csv / extreme.csv / insane.csv / torment.csv
pause
