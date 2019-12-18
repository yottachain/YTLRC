YTASODIR=/usr/lib/ytalrc
BAKYTADIR=/usr/lib/bak_ytalrc
YTASONAME=libytalrc.so
YTASOHEADER=ytlrc.h
PKGNAME=lrcpkg
cc=gcc
install:
	if [ -d $(BAKYTADIR) ];then \
                rm -rf $(BAKYTADIR); \
        fi 
	if [ -d $(YTASODIR) ];then \
		cp -r $(YTASODIR) $(BAKYTADIR); \
		rm -rf $(YTASODIR); \
	fi 
	mkdir -p $(YTASODIR);
	cp $(YTASONAME) $(YTASODIR);
	cp $(YTASOHEADER) $(YTASODIR);
	if cat /etc/profile |grep $(YTASODIR);then \
		echo 'LD_LIBRARY_PATH has been set before' ; \
	else \
		echo 'export LD_LIBRARY_PATH=$$LD_LIBRARY_PATH:$(YTASODIR)' >> /etc/profile ; \
	fi
	if [ -d $${GOROOT}/src/$(PKGNAME) ];then \
                rm -rf $${GOROOT}/src/$(PKGNAME); \
        fi 
	cp -r $(PKGNAME) $${GOROOT}/src/
