@run clear
uname -r
lsmod | grep '^pf ' || echo 'pf: not loaded yet'
