clean:
	rm -f /p/a/t/h dina.exe
build: clean
	g++ -mwindows main.cpp -o dina.exe -lwinhttp -luser32 -lgdi32
