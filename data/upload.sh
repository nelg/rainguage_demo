for file in `ls -A1`; do curl -F "file=@$PWD/$file" rainguage.local/edit; done
