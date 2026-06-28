# TOON-vs-JSON token-savings harness for zsv output surfaces.
# Metric: token count (lower = more ergonomic for an LLM consumer).
# Pipeline: corpus (zsv -> json) -> toon (json2toon) -> count (tiktoken) -> report.

VENV := venv/bin/python
C    := corpus
O    := out

.PHONY: help
help:                       ## show this help
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
	  awk 'BEGIN{FS=":.*?## "}{printf "  %-12s %s\n",$$1,$$2}'

.PHONY: all
all: report                 ## run the whole pipeline

venv:                       ## create venv with tiktoken
	python3 -m venv venv && venv/bin/pip install -q tiktoken

.PHONY: corpus
corpus:                     ## generate JSON corpus from the live zsv binary
	@mkdir -p $(C) $(O)
	zsv commands --json                  > $(C)/commands.json
	zsv help formula-functions-json      > $(C)/formula-functions.json
	zsv help formats-json                > $(C)/formats.json
	zsv help json-schema                 > $(C)/json-schema.json
	zsv help compare-json-redline-schema > $(C)/redline-schema.json
	python3 gen_people.py $(C)/people.csv
	zsv 2json     $(C)/people.csv > $(C)/people-2json.json
	zsv desc+ -a -j $(C)/people.csv > $(C)/people-descplus.json

.PHONY: toon
toon: corpus                ## convert every JSON to TOON
	@for f in $(C)/*.json; do b=$$(basename $$f .json); json2toon -i $$f -o $(O)/$$b.toon; done

.PHONY: report
report: toon venv           ## count tokens and print the comparison table
	@$(VENV) count.py
