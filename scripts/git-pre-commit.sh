#!/bin/bash

shopt -s globstar

for i in **/*.rs; do
	if head -n1 $i | grep -v '^/\*' > /dev/null; then
		exit 1
	fi
done

cargo +nightly fmt --check --quiet
