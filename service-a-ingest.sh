#!/bin/sh

curl -F "file=@meteo.csv" "http://localhost:8080/ingest/csv"

echo -e "\n"