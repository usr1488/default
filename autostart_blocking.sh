#!/usr/bin/sh
xrandr -s 1920x1200
feh --no-fehbg --bg-fill $HOME/Pictures/background.jpg

# need for windows opacity
if [ -z $(pgrep xcompmgr) ]
then
	xcompmgr 1> /dev/null 2> /dev/null &
fi

TOTAL_RAM=$(free -h | awk '/Mem:/ {print $2}')

bat() {
	if ! [ -e /sys/class/power_supply/BAT0 ]
	then
		echo n/a
	elif [ $(cat /sys/class/power_supply/BAT0/status | tr -d '\n') == 'Charging' ]
	then
		echo "$(cat /sys/class/power_supply/BAT0/capacity)+%"
	else
		echo "$(cat /sys/class/power_supply/BAT0/capacity)%"
	fi
}

while true
do
	CURRENT_RBYTES=$(($(cat /sys/class/net/*/statistics/rx_bytes | paste -sd +)))
	CURRENT_SBYTES=$(($(cat /sys/class/net/*/statistics/tx_bytes | paste -sd +)))

	xsetroot -name "$(free -h | awk '/Mem:/ {print $3}')/$TOTAL_RAM | r: $(numfmt $((CURRENT_RBYTES - PREV_RBYTES)) --to=iec-i) s: $(numfmt $((CURRENT_SBYTES - PREV_SBYTES)) --to=iec-i) | $(setxkbmap -query | awk '/layout:/ {print $2}') | $(bat) | $(date '+%d/%m/%Y %H:%M:%S %a %b')"

	PREV_RBYTES=$CURRENT_RBYTES
	PREV_SBYTES=$CURRENT_SBYTES

	sleep 1
done &

