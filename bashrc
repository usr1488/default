#!/usr/bin/bash
export _JAVA_AWT_WM_NONREPARENTING=1
alias ls='ls --color=auto'
alias clear='tput reset'

create_ps1() {
	local GIT_BRANCH=''
	local GIT_STATUS=''
	

	if [ -d '.git' ]
	then
		GIT_BRANCH=" $(git branch --show-current | tr -d '\n')"

		if ! [ -z "$(git status -s)" ]
		then
			GIT_STATUS='+'
		else
			GIT_STATUS=''
		fi
	fi

	PS1="[\u \W${GIT_BRANCH}${GIT_STATUS}]\$ "
}

PROMPT_COMMAND=create_ps1

