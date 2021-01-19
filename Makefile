LAB = lab4c

$(LAB): lab4c_tcp.c lab4c_tls.c
	gcc lab4c_tcp.c -o lab4c_tcp -lmraa -lm -Wall -Wextra -std=c99
	gcc lab4c_tls.c -o lab4c_tls -lmraa -lm -lssl -lcrypto -Wall -Wextra -std=c99

dist: clean
	tar --exclude=$(LAB).tar.gz -cvzf $(LAB).tar.gz *

clean:
	rm -f lab4c_tcp lab4c_tls *.tar.gz
