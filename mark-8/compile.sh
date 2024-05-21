#! /bin/sh

gcc ./net/first-worker.c ./net/client-tools.c ./util/parser.c -g3 -O1 -Wall -Wextra -Wpedantic -Wsign-conversion -lrt -lm -o first-worker
gcc ./net/second-worker.c ./net/client-tools.c ./util/parser.c -g3 -O1 -Wall -Wextra -Wpedantic -Wsign-conversion -lrt -lm -o second-worker
gcc ./net/third-worker.c ./net/client-tools.c ./util/parser.c -g3 -O1 -Wall -Wextra -Wpedantic -Wsign-conversion -lrt -lm -o third-worker
gcc ./net/logs-collector.c ./net/client-tools.c ./util/parser.c -g3 -O1 -Wall -Wextra -Wpedantic -Wsign-conversion -lrt -lm -o logs-collector
gcc ./net/server.c ./net/server-tools.c ./util/parser.c -g3 -O1 -Wall -Wextra -Wpedantic -Wsign-conversion -lrt -lpthread -o server
