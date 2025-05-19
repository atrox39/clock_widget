clean:
	rm -f /p/a/t/h Clock.exe
build: clean
	g++ -mwindows main.cpp -o Clock.exe -lwinhttp -luser32 -lgdi32
