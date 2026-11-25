FF_OBJ:=\
	 $(FOXFANG_SRC)/main.o

foxfang: $(FF_OBJ) $(BINDIR) $(UTILS_OBJ)
	$(CC) -o $(BINDIR)/foxfang $(FF_OBJ) $(UTILS_OBJ) $(LDFLAGS)

$(FF_OBJ):%.o: %.c
	$(CC) -o $@ $(CFLAGS) -c $<

-include $(FF_OBJ:.o=.d)

