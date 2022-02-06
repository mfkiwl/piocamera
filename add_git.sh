#!/bin/bash
NOW=`date "+%Y%m%d_%H%M%S"`
git add .
# git commit -m "automatically uploaded at "$NOW
git commit -m "Automatically uploaded"
git push origin main
