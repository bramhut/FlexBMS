#!/bin/bash

git pull

npm run build

cp -r dist/* /var/www/bms
