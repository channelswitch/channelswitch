#!/bin/sh
# In $1.csv:
# Type, Inputs, Period, MHz, LUT_ff_pairs, Synth_Time, RAM_Bits

RESULTS_CSV="$1"
FIELDS="Type Inputs Period MHz LUT_ff_pairs Synth_Time RAM_Bits"

# $1 - name of field
get_field_number() {
	j=1
	for i in $FIELDS; do
		if [ "$i" = "$1" ]; then
			printf "%d" $j
		fi
		j=$(expr $j + 1)
	done
}

# $1 - the output file
# $2 - fields (will be sorted by the first field)
# $3 - filter variable
# $4 - its value
process_results() {
	exec 3>$1
	LAST=""
	for i in $2; do
		if [ -n "$LAST" ]; then
			printf "$LAST, "
		fi
		LAST="$i"
	done >&3
	printf "$LAST\n" >&3
	cat $RESULTS_CSV | while read line; do
		j=1
		for i in $FIELDS; do
			eval "$i=$(echo $line | cut -d, -f$j | xargs)"
			j=$(expr $j + 1)
		done
		if [ "$(eval "printf "%s" \$$3")" -eq "$4" ]; then
			LAST=""
			for i in $2; do
				if [ -n "$LAST" ]; then
					printf "%s, " "$LAST"
				fi
				LAST=$(echo $line | cut -d, -f$(get_field_number $i) | xargs)
			done
			printf "%s\n" "$LAST"
		fi
	done  | sort -n >&3
	exec 3>&-
}

echo 0/6
process_results InputsMHz.csv "Inputs MHz Type" Period 512
echo 1/6
process_results PeriodMHz.csv "Period MHz Type" Inputs 8
echo 2/6
process_results InputsPairs.csv "Inputs LUT_ff_pairs Type" Period 512
echo 3/6
process_results PeriodPairs.csv "Period LUT_ff_pairs Type" Inputs 8
echo 4/6
process_results InputsBits.csv "Inputs RAM_Bits Type" Period 512
echo 5/6
process_results PeriodBits.csv "Period RAM_Bits Type" Inputs 8
echo 6/6
touch all_csv_files
