#/bin/bash
set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
SEP=----------------------------------------------------------------------------

touch $SCRIPT_DIR/del-me-to-stop
#echo > $SCRIPT_DIR/output.txt

cd $SCRIPT_DIR/../zephyr/doc
rm -Rf _build
make configure
cd _build
ninja doxygen
python $SCRIPT_DIR/main.py --save-input=$SCRIPT_DIR/../old.pkl $SCRIPT_DIR/../zephyr/doc/_build/doxygen/xml

while [ -f $SCRIPT_DIR/del-me-to-stop ]
do
	set +e
	git diff-tree --no-commit-id --name-only HEAD -r | grep '^include\/'
	NOT_FOUND=$?
	set -e
	if [ $NOT_FOUND -ne 0 ]; then
		echo SKIPPING, NO PUBLIC HEADER CHANGE
		echo $SEP >> $SCRIPT_DIR/output.txt
		echo `git log --pretty=oneline --abbrev-commit -n 1` - SKIPPING, NO PUBLIC HEADER CHANGE >> $SCRIPT_DIR/output.txt
		git checkout HEAD^
		continue
	fi
	rm -f $SCRIPT_DIR/../new.pkl
	mv $SCRIPT_DIR/../old.pkl $SCRIPT_DIR/../new.pkl
	cd $SCRIPT_DIR/../zephyr/doc
	echo $SEP
	git --no-pager log -n 1
	git --no-pager log -n 1 > $SCRIPT_DIR/log-long.tmp.txt
	git log --pretty=oneline --abbrev-commit -n 1 > $SCRIPT_DIR/log-short.tmp.txt
	git checkout HEAD^
	rm -Rf _build
	make configure
	cd _build
	ninja doxygen
	set +e
	python $SCRIPT_DIR/main.py \
		--save-old-input=$SCRIPT_DIR/../old.pkl \
		$SCRIPT_DIR/../new.pkl \
		$SCRIPT_DIR/../zephyr/doc/_build/doxygen/xml \
		> $SCRIPT_DIR/result.tmp.txt
	LEVEL=$?
	set -e
	if [ $LEVEL -eq 0 ]; then
		echo $SEP >> $SCRIPT_DIR/output.txt
		echo `cat $SCRIPT_DIR/log-short.tmp.txt` - NOTHING DETECTED >> $SCRIPT_DIR/output.txt
	elif [ $LEVEL -le 3 ]; then
		echo $SEP >> $SCRIPT_DIR/output.txt
		cat $SCRIPT_DIR/log-long.tmp.txt >> $SCRIPT_DIR/output.txt
		cat $SCRIPT_DIR/result.tmp.txt >> $SCRIPT_DIR/output.txt
	else
		exit $LEVEL
	fi
	#rm -f $SCRIPT_DIR/*.tmp.txt
	#rm $SCRIPT_DIR/del-me-to-stop
done
