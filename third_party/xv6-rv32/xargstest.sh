echo hello > b
mkdir a
echo hello > a/b
mkdir a/aa
echo hello > a/aa/b
find . b | xargs grep hello
rm a/aa/b
rm a/b
rm a/aa
rm a
rm b
