# Environment 
CFLAGS += $(shell pkg-config --cflags fuse)
LIBS += $(shell pkg-config --libs fuse)
CC=gcc

# x360 (fuse filesystem driver)
x360: x360.o x360_fr.o
		$(CC) -o x360 x360.o x360_fr.o $(LIBS)


# build
build: .build-post

.build-pre:
# Add your pre 'build' code here...

.build-post:
# Add your post 'build' code here...


# clean
clean: .clean-post
		$(RM) x360

.clean-pre:
# Add your pre 'clean' code here...

.clean-post:
# Add your post 'clean' code here...


# clobber
clobber: .clobber-post

.clobber-pre:
# Add your pre 'clobber' code here...

.clobber-post:
# Add your post 'clobber' code here...


# all
all: .all-post

.all-pre:
# Add your pre 'all' code here...

.all-post:
# Add your post 'all' code here...


# help
help: .help-post

.help-pre:
# Add your pre 'help' code here...

.help-post:
# Add your post 'help' code here...

