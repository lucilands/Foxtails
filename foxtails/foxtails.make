FT_OBJ:=\
	 $(FOXTAIL_SRC)/main.o\
     $(FOXTAIL_SRC)/worker_loop.o\
     $(FOXTAIL_SRC)/foxctl.o

foxtails: $(FT_OBJ) $(BINDIR) $(UTILS_OBJ)
	$(CC) -o $(BINDIR)/foxtails $(FT_OBJ) $(UTILS_OBJ) $(LDFLAGS)

$(FT_OBJ):%.o: %.c
	$(CC) -o $@ $(CFLAGS) -c $<

-include $(FT_OBJ:.o=.d)
