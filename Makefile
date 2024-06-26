### i686-w64-mingw32 ###
# g++.exe (i686-posix-sjlj, built by strawberryperl.com project) 4.9.2

CC = g++.exe

CFLAGS = -Wall -O2 -std=c++11
#  -Wl,--stack,16777216  -m32

OBJSDIR = objs

OBJS = $(OBJSDIR)/c2h5oh.o \
	$(OBJSDIR)/response.o \
	$(OBJSDIR)/cgi.o \
	$(OBJSDIR)/fcgi.o \
	$(OBJSDIR)/scgi.o \
	$(OBJSDIR)/config.o \
	$(OBJSDIR)/socket.o \
	$(OBJSDIR)/log.o \
	$(OBJSDIR)/create_headers.o \
	$(OBJSDIR)/range.o \
	$(OBJSDIR)/encoding.o \
	$(OBJSDIR)/functions.o \
	$(OBJSDIR)/manager.o \
	$(OBJSDIR)/event_handler.o \
	$(OBJSDIR)/index.o \

c2h5oh.exe: $(OBJS)
	$(CC) $(CFLAGS) -o $@  $(OBJS)  -lwsock32 -lws2_32 -static

$(OBJSDIR)/c2h5oh.o: c2h5oh.cpp main.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJSDIR)/config.o: config.cpp main.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJSDIR)/socket.o: socket.cpp main.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJSDIR)/log.o: log.cpp main.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJSDIR)/create_headers.o: create_headers.cpp main.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJSDIR)/response.o: response.cpp main.h range.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJSDIR)/range.o: range.cpp main.h range.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJSDIR)/event_handler.o: event_handler.cpp main.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJSDIR)/encoding.o: encoding.cpp main.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJSDIR)/functions.o: functions.cpp main.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJSDIR)/send_files.o: send_files.cpp main.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJSDIR)/manager.o: manager.cpp main.h range.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJSDIR)/cgi.o: cgi.cpp main.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJSDIR)/fcgi.o: fcgi.cpp main.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJSDIR)/scgi.o: scgi.cpp main.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJSDIR)/index.o: index.cpp main.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	del -f c2h5oh.exe
	del -f $(OBJSDIR)\*.o
