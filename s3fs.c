/* This code is based on the fine code written by Joseph Pfeiffer for his
   fuse system tutorial. */

#include "s3fs.h"
#include "libs3_wrapper.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <pwd.h>
#include <time.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/xattr.h>

#define GET_PRIVATE_DATA ((s3context_t *) fuse_get_context()->private_data)

int fs_mkdir(const char *, mode_t);

/*
 * For each function below, if you need to return an error,
 * read the appropriate man page for the call and see what
 * error codes make sense for the type of failure you want
 * to convey.  For example, many of the calls below return
 * -EIO (an I/O error), since there are no S3 calls yet
 * implemented.  (Note that you need to return the negative
 * value for an error code.)
 */

/* *************************************** */
/*        Stage 1 callbacks                */
/* *************************************** */

/*
 * Initialize the file system.  This is called once upon
 * file system startup.
 */
void *fs_init(struct fuse_conn_info *conn)
{
	fprintf(stderr, "fs_init --- initializing file system.\n");
	s3context_t *ctx = GET_PRIVATE_DATA;
	if (s3fs_test_bucket(ctx->s3bucket) < 0)
	{
		fprintf(stderr, "Failed to connect to bucket (s3fs_test_bucket)\n");
	}
	else
	{
		fprintf(stderr, "Successfully connected to bucket (s3fs_test_bucket)\n");
	}
	if (s3fs_clear_bucket(ctx->s3bucket) < 0)
	{
		fprintf(stderr, "Failed to clear bucket (s3fs_clear_bucket)\n");
	}
	else
	{
		fprintf(stderr, "Successfully cleared the bucket (removed all objects)\n");
	}
	s3dirent_t * newent = (s3dirent_t *) malloc(sizeof(s3dirent_t));
	strcpy((newent->name),".");
	newent->type = 'D';
	newent->size = sizeof(s3dirent_t);
	newent->permissions = (S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR);
	newent->hardlinks = 1;
	newent->user = getuid();
	newent->group = getgid(); 
	time_t now = time(NULL);
        ctime(&now);
	newent->modify = now;
	newent->access = now;
	newent->change = now;	
        ssize_t test = s3fs_put_object(ctx->s3bucket, "/", (uint8_t*)newent, sizeof(s3dirent_t));
	free(newent);
	if(test < 0){
		fprintf(stderr, "initialization failed.\n");
	}
	else if(test < sizeof(s3dirent_t)){
		fprintf(stderr, "did not allocate full dirent.\n");
	}
	return (ctx->s3bucket);
}

/*
 * Clean up filesystem -- free any allocated data.
 * Called once on filesystem exit.
 */
void fs_destroy(void *userdata) {
    fprintf(stderr, "fs_destroy --- shutting down file system.\n");
    free(userdata);
}


void fillstat(s3dirent_t dirent, struct stat *statbuf)
{
	
	statbuf->st_mode = dirent.permissions;
	statbuf->st_nlink = dirent.hardlinks;
	statbuf->st_uid = dirent.user;
	statbuf->st_gid = dirent.group;
	statbuf->st_size = dirent.size;
	statbuf->st_atime = dirent.access;
	statbuf->st_mtime = dirent.modify;
	statbuf->st_ctime = dirent.change;
	statbuf->st_blocks = (dirent.size/512) + 1; //????????
	printf("%s%d\n\n\n", "fillstat size: ", statbuf->st_size);
}


/*
 * Get file attributes.  Similar to the stat() call
 * (and uses the same structure).  The st_dev, st_blksize,
 * and st_ino fields are ignored in the struct (and
 * do not need to be filled in).
 */

int fs_getattr(const char *path, struct stat *statbuf) {
    fprintf(stderr, "fs_getattr(path=\"%s\")\n", path);
    s3context_t *ctx = GET_PRIVATE_DATA;
    s3dirent_t * buffer = NULL;
    struct fuse_file_info *fi;
    char *bucket = (ctx->s3bucket);
    if (!fs_opendir(path,  fi))//is a directory
    {
    	if(s3fs_get_object(bucket, path, (uint8_t**)&buffer, 0,0)==-1)
   	{
		free(buffer);
		printf("%s\n\n\n\n\n\n\n\n\n\n\n","option0");

		return -ENOENT;
   	}
	else
	{
		fillstat(buffer[0], statbuf);
		printf("%s%d\n\n\n", "statbuf size: ", statbuf->st_size);
		free(buffer);
		return 0;
	}
    }
    else //is a file or error
    {
	char * pat = strdup(path);
	char * dir = dirname(pat);
	if(s3fs_get_object(bucket, path, (uint8_t**)&buffer, 0,0)==-1)
	{
		free(buffer);
		free(pat);
		printf("%s\n\n\n\n\n\n\n\n\n","option1");

		return -ENOENT;
	}
	if(s3fs_get_object(bucket, dir, (uint8_t**)&buffer, 0,0)==-1)
	{
		free(buffer);
		free(pat);
		printf("%s\n\n\n\n\n\n\n\n\n\n","option2");
		return -ENOENT;
	}
	else
	{
		int length = sizeof(buffer)/sizeof(s3dirent_t);
		int x = 0;
		char * dup = strdup(path);
		for(; x < length; x++)
		{
			s3dirent_t dirent = buffer[x];
			if(strcmp((dirent.name), basename(dup)) == 0)
			{
				fillstat(buffer[x],statbuf);
				free(pat);
				free(buffer);
				free(dup);
				return 0;
			}
		}
		free(pat);
        	free(buffer);
        	free(dup);
        	return -EIO;
	}
    }
}


/*
 * Open directory
 *
 * This method should check if the open operation is permitted for
 * this directory
 */
int fs_opendir(const char *path, struct fuse_file_info *fi) {
    fprintf(stderr, "fs_opendir(path=\"%s\")\n", path);
    s3context_t *ctx = GET_PRIVATE_DATA;
    if (!strcasecmp(path, "/")) //root entered on initialization
    {
	return 0;
    }
    char * pat = strdup(path);
    char * dir = dirname(pat);
    s3dirent_t *buffer = NULL;
	char * bucket = (ctx->s3bucket);
    if(s3fs_get_object(bucket, path, (uint8_t**)&buffer, 0, 0) == -1)
    {
	free(buffer);
	free(pat);
	printf(stderr, "%s\n\n\n\n\n\n","option3");
	printf("%s",path);
	return -ENOENT;
    }
	free(buffer);
	printf("%s\n", "test2");
    int success = s3fs_get_object(bucket, dir, (uint8_t**)&buffer, 0, 0);
    free(pat);
	if(success == -1)
    {
	free(buffer);
	printf("%s\n\n\n\n\n\n","option4");
	return -ENOENT;
    }
	printf(stderr, "%s\n", "test2.5");
	int x = 0;
	int length = success/sizeof(s3dirent_t);
	char * dup = strdup(path);
	for(; x < length; x++)
	{
		printf("%s%d\n", "test3", x);
		s3dirent_t dirent = buffer[x];
		if(strcmp((dirent.name), basename(dup)) == 0)
		{
			if(dirent.type == 'D')
			{
				free(dup);
				free(buffer);
				return 0;
			}
			else if (dirent.type == 'F')
			{
				free(dup);
				free(buffer);
				return -ENOTDIR;
			}
		}
	}
	free(buffer);
	printf(stderr, "%s\n\n\n\n\n\n\n\n\n\n","option6");
	free(dup);
	return -ENOENT;
}


/*
 * Read directory.  See the project description for how to use the filler
 * function for filling in directory items.
 */
int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
         struct fuse_file_info *fi)
{
    	fprintf(stderr, "fs_readdir(path=\"%s\", buf=%p, offset=%d)\n",
        path, buf, (int)offset);
    	s3context_t *ctx = GET_PRIVATE_DATA;
	int test = fs_opendir(path, fi);
	if(test){
		return test;
	}
	char *bucket = (ctx->s3bucket);
	s3dirent_t * buffer = NULL;
	int success = s3fs_get_object(bucket, path, (uint8_t**)&buffer, 0, 0);
	int length = success/sizeof(s3dirent_t);
        int x = 0;
	for(; x < length; x++)
        {
		if (buffer[x].type != 'U')
    	        {
			if(filler(buf, buffer[x].name, NULL, 0)!=0)
        	        {
	               		free(buffer);
				return -ENOMEM;
                	}
		}
        }
	free(buffer);
    return 0;
}


/*
 * Release directory.
 */
int fs_releasedir(const char *path, struct fuse_file_info *fi) {
    fprintf(stderr, "fs_releasedir(path=\"%s\")\n", path);
    s3context_t *ctx = GET_PRIVATE_DATA;
    return 0;
}


int adddirent(const char *path, mode_t mode, char * bucket)
{
	s3dirent_t * newent = (s3dirent_t *) malloc(sizeof(s3dirent_t));
	strcpy((newent->name),"."); 
	newent->type = 'D';
	newent->size = sizeof(s3dirent_t);
	newent->permissions = mode;
	newent->hardlinks = 1;
	newent->user = getuid();
	newent->group = getgid(); 
	time_t now = time(NULL);
        ctime(&now);
	newent->modify = now;
	newent->access = now;
	newent->change = now;
        int test = s3fs_put_object(bucket, path, (uint8_t*)newent, sizeof(s3dirent_t)); 
	free(newent);
	if(test < 0){
                fprintf(stderr, "upload failed.\n");
		return -EIO;
        }
        else if(test < sizeof(s3dirent_t)){
                fprintf(stderr, "did not allocate full dirent.\n");
                return -EIO;
        }
        return 0;
}

int adddirtoparent(const char * path, char * bucket)
{
	char * pat = strdup(path);
	char * par = dirname(pat);
	char * dup = strdup(path);
	s3dirent_t * buffer = NULL;
	int success = s3fs_get_object(bucket, par, (uint8_t**)&buffer, 0, 0);
	if(success ==-1)
	{
		free(buffer);
        	free(dup);
		free(pat);
		return -EIO;
	}
	int length = success/sizeof(s3dirent_t);
	s3dirent_t * newents = (s3dirent_t *) malloc(sizeof(s3dirent_t)*(length+1));
	int x = 0;
	buffer[0].hardlinks++;
	for (;x<length;x++)
	{
		newents[x] = buffer[x];
	}
	s3dirent_t adding;
	adding.type = 'D';
	strcpy(adding.name, basename(dup));
	adding.size = sizeof(s3dirent_t);
	newents[x] = adding;
	int remove = s3fs_remove_object(bucket, par);
	if(remove < 0){
		free(buffer);
        	free(dup);
		free(pat);
		return -EIO;
	}
	int test = s3fs_put_object(bucket, par, (uint8_t *)newents, (length + 1)*sizeof(s3dirent_t));
	free(buffer);
	free(newents);
	free(pat);
	free(dup);
	return test;
}

/*
 * Create a new directory.
 *
 * Note that the mode argument may not have the type specification
 * bits set, i.e. S_ISDIR(mode) can be false.  To obtain the
 * correct directory type bits (for setting in the metadata)
 * use mode|S_IFDIR.
 */

int fs_mkdir(const char *path, mode_t mode) {
    fprintf(stderr, "fs_mkdir(path=\"%s\", mode=0%3o)\n", path, mode);
    s3context_t *ctx = GET_PRIVATE_DATA;
    char * bucket = (ctx->s3bucket);
	struct fuse_file_info * fi;
    mode |= S_IFDIR;
    if(!fs_opendir(path, fi))//directory already exists
    {
        return -EEXIST;
    }
    else
    {
	char * pat = strdup(path);
	char * par = dirname(pat);
	if(fs_opendir(par,fi)) //check if parent exists
	{
		return -ENOENT;
	}
	free(pat);
	int test = adddirtoparent(path, bucket);
	if(test < 0){
                fprintf(stderr, "upload failed.\n");
                return -EIO;
        }
        else if(test < sizeof(s3dirent_t)){
                fprintf(stderr, "did not allocate full dirent.\n");
                return -EIO;
        }
	return adddirent(path, mode, bucket);
    }
}




/*
 * Remove a directory.
 */
int fs_rmdir(const char *path) {
    fprintf(stderr, "fs_rmdir(path=\"%s\")\n", path);
    s3context_t *ctx = GET_PRIVATE_DATA;
    struct fuse_file_info *fi;
	int testingnum = fs_opendir(path, fi); //check if directory
	if(testingnum){ //check if directory
		fprintf("%s\n\n\n\n\n", "option remove");
		return testingnum;
	}
	s3dirent_t * buffer = NULL;
	char * bucket = (ctx->s3bucket);
	int test = s3fs_get_object(ctx->s3bucket, path, (uint8_t**)&buffer, 0, 0);
	if(test == -1){
		free(buffer);
		fprintf("%s\n\n\n\n\n\n", "option the last");
		return -ENOENT;
	}
	//we now know that it is here and a directory
	int x = 1;
	int length = test/sizeof(s3dirent_t);
	for(; x < length; x++){	//make sure that the dirent array is "empty"
		if(buffer[x].type != 'U'){
			free(buffer);
			return -ENOTEMPTY;
		}
	}
	if(s3fs_remove_object(bucket, path) == -1){
		free(buffer);
		return -EIO;
	}
	char *pat = strdup(path);
	char *par = dirname(pat);
	free(buffer);
	buffer = NULL;
	//now to update parent
	test = s3fs_get_object(bucket, par, (uint8_t**)&buffer, 0, 0);
	if(test == -1){
		free(buffer);
		fprintf("%s\n\n\n\n\n", "Just kidding");
                return -ENOENT;
        }
        x = 1;
        length = test/sizeof(s3dirent_t);
	char * dup = strdup(path);
        for(; x < length; x++){
                if(!strcmp(buffer[x].name, basename(dup))){
                        if(buffer[x].type != 'U')
			{
				buffer[x].type = 'U';
               		 	buffer[0].hardlinks--;
				 if(s3fs_remove_object(bucket, par) == -1){
	        		        free(buffer);
					free(dup);
					free(pat);
                			return -EIO;
        			}
        			int test2 = s3fs_put_object(bucket, par, (uint8_t *)buffer, (length)*sizeof(s3dirent_t));
  	      			if(test2 < 0){
             		  	  fprintf(stderr, "upload failed.\n");
            			    	free(dup);
					free(pat);
					free(buffer);
					return -EIO;
       				 }
     				   else if(test2 < sizeof(s3dirent_t)){
               			 fprintf(stderr, "did not allocate full dirent.\n");
      			          	free(dup);
					free(buffer);
					free(pat);
					return -EIO;
        			}
				free(dup);
				free(buffer);
				free(pat);
				return 0;
			}
		}
        }
	free(dup);
	free(pat);
	free(buffer);
	return -EIO;
}


/* *************************************** */
/*        Stage 2 callbacks                */
/* *************************************** */


/* 
 * Create a file "node".  When a new file is created, this
 * function will get called.  
 * This is called for creation of all non-directory, non-symlink
 * nodes.  You *only* need to handle creation of regular
 * files here.  (See the man page for mknod (2).)
 */
int fs_mknod(const char *path, mode_t mode, dev_t dev) {
    fprintf(stderr, "fs_mknod(path=\"%s\", mode=0%3o)\n", path, mode);
    s3context_t *ctx = GET_PRIVATE_DATA;
    return -EIO;
}


/* 
 * File open operation
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  
 * 
 * Optionally open may also return an arbitrary filehandle in the 
 * fuse_file_info structure (fi->fh).
 * which will be passed to all file operations.
 * (In stages 1 and 2, you are advised to keep this function very,
 * very simple.)
 */
int fs_open(const char *path, struct fuse_file_info *fi) {
    fprintf(stderr, "fs_open(path\"%s\")\n", path);
    s3context_t *ctx = GET_PRIVATE_DATA;
    return -EIO;
}


/* 
 * Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  
 */
int fs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    fprintf(stderr, "fs_read(path=\"%s\", buf=%p, size=%d, offset=%d)\n",
          path, buf, (int)size, (int)offset);
    s3context_t *ctx = GET_PRIVATE_DATA;
    return -EIO;
}


/*
 * Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.
 */
int fs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    fprintf(stderr, "fs_write(path=\"%s\", buf=%p, size=%d, offset=%d)\n",
          path, buf, (int)size, (int)offset);
    s3context_t *ctx = GET_PRIVATE_DATA;
    return -EIO;
}


/*
 * Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.  
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 */
int fs_release(const char *path, struct fuse_file_info *fi) {
    fprintf(stderr, "fs_release(path=\"%s\")\n", path);
    s3context_t *ctx = GET_PRIVATE_DATA;
    return -EIO;
}


/*
 * Rename a file.
 */
int fs_rename(const char *path, const char *newpath) {
    fprintf(stderr, "fs_rename(fpath=\"%s\", newpath=\"%s\")\n", path, newpath);
    s3context_t *ctx = GET_PRIVATE_DATA;
    return -EIO;
}


/*
 * Remove a file.
 */
int fs_unlink(const char *path) {
    fprintf(stderr, "fs_unlink(path=\"%s\")\n", path);
    s3context_t *ctx = GET_PRIVATE_DATA;
    return -EIO;
}
/*
 * Change the size of a file.
 */
int fs_truncate(const char *path, off_t newsize) {
    fprintf(stderr, "fs_truncate(path=\"%s\", newsize=%d)\n", path, (int)newsize);
    s3context_t *ctx = GET_PRIVATE_DATA;
    return -EIO;
}


/*
 * Change the size of an open file.  Very similar to fs_truncate (and,
 * depending on your implementation), you could possibly treat it the
 * same as fs_truncate.
 */
int fs_ftruncate(const char *path, off_t offset, struct fuse_file_info *fi) {
    fprintf(stderr, "fs_ftruncate(path=\"%s\", offset=%d)\n", path, (int)offset);
    s3context_t *ctx = GET_PRIVATE_DATA;
    return -EIO;
}


/*
 * Check file access permissions.  For now, just return 0 (success!)
 * Later, actually check permissions (don't bother initially).
 */
int fs_access(const char *path, int mask) {
    fprintf(stderr, "fs_access(path=\"%s\", mask=0%o)\n", path, mask);
    s3context_t *ctx = GET_PRIVATE_DATA;
    return 0;
}


/*
 * The struct that contains pointers to all our callback
 * functions.  Those that are currently NULL aren't 
 * intended to be implemented in this project.
 */
struct fuse_operations s3fs_ops = {
  .getattr     = fs_getattr,    // get file attributes
  .readlink    = NULL,          // read a symbolic link
  .getdir      = NULL,          // deprecated function
  .mknod       = fs_mknod,      // create a file
  .mkdir       = fs_mkdir,      // create a directory
  .unlink      = fs_unlink,     // remove/unlink a file
  .rmdir       = fs_rmdir,      // remove a directory
  .symlink     = NULL,          // create a symbolic link
  .rename      = fs_rename,     // rename a file
  .link        = NULL,          // we don't support hard links
  .chmod       = NULL,          // change mode bits: not implemented
  .chown       = NULL,          // change ownership: not implemented
  .truncate    = fs_truncate,   // truncate a file's size
  .utime       = NULL,          // update stat times for a file: not implemented
  .open        = fs_open,       // open a file
  .read        = fs_read,       // read contents from an open file
  .write       = fs_write,      // write contents to an open file
  .statfs      = NULL,          // file sys stat: not implemented
  .flush       = NULL,          // flush file to stable storage: not implemented
  .release     = fs_release,    // release/close file
  .fsync       = NULL,          // sync file to disk: not implemented
  .setxattr    = NULL,          // not implemented
  .getxattr    = NULL,          // not implemented
  .listxattr   = NULL,          // not implemented
  .removexattr = NULL,          // not implemented
  .opendir     = fs_opendir,    // open directory entry
  .readdir     = fs_readdir,    // read directory entry
  .releasedir  = fs_releasedir, // release/close directory
  .fsyncdir    = NULL,          // sync dirent to disk: not implemented
  .init        = fs_init,       // initialize filesystem
  .destroy     = fs_destroy,    // cleanup/destroy filesystem
  .access      = fs_access,     // check access permissions for a file
  .create      = NULL,          // not implemented
  .ftruncate   = fs_ftruncate,  // truncate the file
  .fgetattr    = NULL           // not implemented
};



/* 
 * You shouldn't need to change anything here.  If you need to
 * add more items to the filesystem context object (which currently
 * only has the S3 bucket name), you might want to initialize that
 * here (but you could also reasonably do that in fs_init).
 */
int main(int argc, char *argv[]) {
	fflush(stdout);
    // don't allow anything to continue if we're running as root.  bad stuff.
    if ((getuid() == 0) || (geteuid() == 0)) {
    	fprintf(stderr, "Don't run this as root.\n");
    	return -1;
    }
    s3context_t *stateinfo = malloc(sizeof(s3context_t));
    memset(stateinfo, 0, sizeof(s3context_t));

    char *s3key = getenv(S3ACCESSKEY);
    if (!s3key) {
        fprintf(stderr, "%s environment variable must be defined\n", S3ACCESSKEY);
        return -1;
    }
    char *s3secret = getenv(S3SECRETKEY);
    if (!s3secret) {
        fprintf(stderr, "%s environment variable must be defined\n", S3SECRETKEY);
        return -1;
    }
    char *s3bucket = getenv(S3BUCKET);
    if (!s3bucket) {
        fprintf(stderr, "%s environment variable must be defined\n", S3BUCKET);
        return -1;
    }
    strncpy((*stateinfo).s3bucket, s3bucket, BUFFERSIZE);

    fprintf(stderr, "Initializing s3 credentials\n");
    s3fs_init_credentials(s3key, s3secret);

    fprintf(stderr, "Totally clearing s3 bucket\n");
    s3fs_clear_bucket(s3bucket);

    fprintf(stderr, "Starting up FUSE file system.\n");
    int fuse_stat = fuse_main(argc, argv, &s3fs_ops, stateinfo);
    fprintf(stderr, "Startup function (fuse_main) returned %d\n", fuse_stat);

    return fuse_stat;
}
