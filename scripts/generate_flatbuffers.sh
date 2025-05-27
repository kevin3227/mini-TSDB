#!/bin/bash
flatc --cpp ../schema/tsdb.fbs
mv *.h ../include/tsdb/
