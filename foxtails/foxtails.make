OBJ:=\
	 $(FOXTAIL_SRC)/main.o\
     $(FOXTAIL_SRC)/worker_loop.o

foxtails: $(OBJ) $(BINDIR) $(UTILS_OBJ)
	$(CC) -o$(BINDIR)/foxtails $(OBJ) $(UTILS_OBJ) $(LDFLAGS)

$(OBJ):%.o: %.c
	$(CC) -o $@ $(CFLAGS) -c $<

-include $(OBJ:.o=.d)
