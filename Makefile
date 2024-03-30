obj = http.o 

compile: bin/http

bin/http: $(obj)
	$(CC) $(obj) -o $@ 

%.o: %.c
	$(CC) -c $< -o $@ 
