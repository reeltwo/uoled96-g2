#ifndef __APPLE__
#define  _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 500L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <ftw.h>
#include <sys/stat.h>
#include <sys/types.h>

static int remove_callback(const char *pathname,
                __attribute__((unused)) const struct stat *sbuf,
                __attribute__((unused)) int type,
                __attribute__((unused)) struct FTW *ftwb)
{
	return remove(pathname);
}

static int framestring_cmp(const void *a, const void *b) 
{ 
    const char **ia = (const char **)a;
    const char **ib = (const char **)b;
	unsigned int iaframe;
	unsigned int ibframe;
	const char* iabase = strrchr(*ia, '/');
	const char* ibbase = strrchr(*ib, '/');
    if (sscanf(iabase, "/frame%u.raw", &iaframe) == 1 &&
    	sscanf(ibbase, "/frame%u.raw", &ibframe) == 1)
    {
    	return (iaframe < ibframe) ? -1 : (iaframe > ibframe) ? 1 : 0;
    }
    return strcmp(*ia, *ib);
} 

int main(int argc, const char* argv[])
{
	unsigned char movieIndex = 0;
 	unsigned numFiles = 0;
	int retcode = 0;
	char cmdbuffer[2048];
	const char* outname = "media.bin";
	unsigned int currentSector = 1;
	FILE* fout = NULL;
	FILE* fin = NULL;
	FILE *cmdf = NULL;
	DIR* dfd = NULL;
	char** filenames = NULL;
	char* tmp_dirname = NULL;
	char auto_play_loop = 0;
	unsigned short auto_play_delay = 0;
	unsigned short auto_play_mask = 0;
	unsigned long numBytesWritten = 0;
	char template[] = "/tmp/tmpdir.XXXXXX";
	unsigned int movieStartSector[100];
	static unsigned char movieHeader[6] = { 0x00, 0x60, 0x00, 0x40, 0x10, 0x21 };
	static unsigned char emptySector[512];

    for (int i = 1; i < argc; i++)
    {
    	if (strcmp(argv[i], "-out") == 0)
    	{
    		if (i + 1 < argc)
    		{
    			outname = argv[i+1];
    			break;
    		}
    		fprintf(stderr, "Missing output filename\n");
    		retcode = 1;
    		goto done;
    	}
    }

	fout = fopen(outname, "wb+");
	if (fout == NULL)
	{
		fprintf(stderr, "Failed to create: %s\n", outname);
		retcode = 1;
		goto done;
	}
	if (fwrite(emptySector, 1, sizeof(emptySector), fout) != sizeof(emptySector))
	{
		fprintf(stderr, "Failed to create: %s\n", outname);
		retcode = 1;
		goto done;
	}
	numBytesWritten += sizeof(emptySector);
	memset(movieStartSector, '\0', sizeof(movieStartSector));

	/* Create the temporary directory */
	tmp_dirname = mkdtemp(template);
    printf("dirname : %s\n", tmp_dirname);
	if (tmp_dirname == NULL)
	{
		fprintf(stderr, "Could not create tmp directory: %s\n", template);
		retcode = 1;
		goto done;
	}

    for (int i = 1; i < argc; i++)
    {
    	unsigned int padBytes = 0;
    	unsigned char hibyte;
    	unsigned char lobyte;
		struct dirent* dp;
		char filbuffer[2048];
		unsigned int fileIndex;
		unsigned int numFrames;
		unsigned int width;
		unsigned int height;
    	const char* moviename = argv[i];
    	const char* scaling_arg = "-s 96x64 ";

    	if (strncmp(moviename, "-auto-play:", strlen("-auto-play:")) == 0)
    	{
    		moviename += strlen("-auto-play:");
    		if (movieIndex < 16)
    		{
	    		auto_play_mask |= (1<<(movieIndex));
    		}
    		else
    		{
    			printf("Warning: Too many auto-play movies. Ignoring: %s\n", moviename);
    		}
    	}
    	else if (strcmp(moviename, "-auto-play-loop") == 0)
    	{
    		auto_play_loop = 1;
    		continue;
    	}
    	else if (strncmp(moviename, "-auto-play-delay:", strlen("-auto-play-delay:")) == 0)
    	{
    		auto_play_delay = atoi(moviename + strlen("-auto-play-delay:"));
    		continue;
    	}
    	else if (strcmp(moviename, "-out") == 0)
    	{
    		i += 1;
    		continue;
    	}

		snprintf(cmdbuffer, sizeof(cmdbuffer), "ffprobe -v error -select_streams v:0 -show_entries stream=nb_frames -of default=nokey=1:noprint_wrappers=1 %s", moviename);
		cmdf = popen(cmdbuffer, "r");
		if (cmdf == NULL)
		{
			fprintf(stderr, "Failed to execute: \"%s\"\n", cmdbuffer);
			retcode = 1;
			goto done;
		}
		if (fscanf(cmdf, "%u", &numFrames) != 1)
		{
			fprintf(stderr, "Failed to get frame count for: %s\n", moviename);
			retcode = 1;
			goto done;
		}
		pclose(cmdf);
		cmdf = NULL;

		if (numFrames > 0xFFFF)
		{
			fprintf(stderr, "Movie too long: %s\n", moviename);
			retcode = 1;
			goto done;
		}


		snprintf(cmdbuffer, sizeof(cmdbuffer), "ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=s=x:p=0 %s", moviename);
		cmdf = popen(cmdbuffer, "r");
		if (cmdf == NULL)
		{
			fprintf(stderr, "Failed to execute: \"%s\"\n", cmdbuffer);
			retcode = 1;
			goto done;
		}
		if (fscanf(cmdf, "%ux%u", &width, &height) != 2)
		{
			fprintf(stderr, "Failed to get resolution for: %s\n", moviename);
			retcode = 1;
			goto done;
		}
		pclose(cmdf);
		cmdf = NULL;

		if (width == 96 && height == 64)
			scaling_arg = "";
		//-framerate 30 
		snprintf(cmdbuffer, sizeof(cmdbuffer), "ffmpeg -i %s -vf \"vflip,hflip\" %s-vsync vfr -pix_fmt rgb565be %s/frame%%04d.raw -hide_banner", moviename, scaling_arg, tmp_dirname);
		if (system(cmdbuffer) != 0)
		{
			fprintf(stderr, "Failed to execute: \"%s\"\n", cmdbuffer);
			retcode = 1;
			goto done;
		}

		movieStartSector[movieIndex] = (unsigned int)(numBytesWritten / 512);
		hibyte = numFrames >> 8;
		lobyte = numFrames & 0xFF;
		if (fwrite(movieHeader, 1, sizeof(movieHeader), fout) != sizeof(movieHeader) ||
			fwrite(&hibyte, 1, 1, fout) != 1 || fwrite(&lobyte, 1, 1, fout) != 1)
		{
			fprintf(stderr, "Failed to write movie header: %s\n", moviename);
			retcode = 1;
			goto done;
		}
		numBytesWritten += sizeof(movieHeader) + 2;

		numFiles = 0;
		if ((dfd = opendir(tmp_dirname)) == NULL)
		{
			fprintf(stderr, "Can't open %s\n", tmp_dirname);
			retcode = 1;
			goto done;
		}
		while ((dp = readdir(dfd)) != NULL)
		{
			struct stat stbuf;
			snprintf(filbuffer, sizeof(filbuffer), "%s/%s", tmp_dirname, dp->d_name);
			if (stat(filbuffer, &stbuf) == 0 && (stbuf.st_mode & S_IFMT) != S_IFDIR)
			{
				unsigned int scratch;
			    if (sscanf(dp->d_name, "frame%u.raw", &scratch) == 1)
			    {
					numFiles++;
			    }
			    else
			    {
			    	printf("NO MATCH : %s\n", filbuffer);
			    }
			}
		}
		rewinddir(dfd);
		fileIndex = 0;
		filenames = (char**)malloc(sizeof(char*) * numFiles);
		if (filenames == NULL)
		{
			fprintf(stderr, "Failed to allocate memory\n");
			retcode = 1;
			goto done;
		}
		memset(filenames, '\0', sizeof(char*) * numFiles);
		while ((dp = readdir(dfd)) != NULL && fileIndex < numFiles)
		{
			struct stat stbuf;
			snprintf(filbuffer, sizeof(filbuffer), "%s/%s", tmp_dirname, dp->d_name);
			if (stat(filbuffer, &stbuf) == 0 && (stbuf.st_mode & S_IFMT) != S_IFDIR)
			{
				unsigned int scratch;
			    if (sscanf(dp->d_name, "frame%u.raw", &scratch) == 1)
					filenames[fileIndex++] = strdup(filbuffer);
			}
		}
		numFiles = fileIndex;
		qsort(filenames, numFiles, sizeof(char*), framestring_cmp);
		closedir(dfd);
		dfd = NULL;

		for (fileIndex = 0; fileIndex < numFiles; fileIndex++)
		{
			int n;
			char* fname = filenames[fileIndex];
			unsigned char sector[sizeof(emptySector)];
			fin = fopen(fname, "rb");
			if (fin == NULL)
			{
				fprintf(stderr, "Failed to open: %s\n", fname);
				retcode = 1;
				goto done;
			}
			while ((n = fread(sector, 1, sizeof(sector), fin)) != 0)
			{
				if (fwrite(sector, 1, n, fout) != n)
				{
					fprintf(stderr, "Failed to copy: %s\n", fname);
					retcode = 1;
					goto done;
				}
				numBytesWritten += n;
			}
			fclose(fin);
			remove(fname);
			free(filenames[fileIndex]);
			filenames[fileIndex] = NULL;
			fin = NULL;
		}
		if (fwrite(emptySector, 1, sizeof(emptySector), fout) != sizeof(emptySector))
		{
			fprintf(stderr, "Failed to create: %s\n", outname);
			retcode = 1;
			goto done;
		}
		numBytesWritten += sizeof(emptySector);
		if ((padBytes = numBytesWritten % sizeof(emptySector)) != 0)
		{
			padBytes = sizeof(emptySector) - padBytes;
			if (fwrite(emptySector, 1, padBytes, fout) != padBytes)
			{
				fprintf(stderr, "Failed to append pad bytes: %s\n", moviename);
				retcode = 1;
				goto done;
			}
			numBytesWritten += (padBytes);
		}
		free(filenames);
		filenames = NULL;
		movieIndex++;
    }
done:
	if (movieIndex != 0)
	{
		unsigned char i;
		unsigned char sector[sizeof(emptySector)];
		unsigned char* sd = sector;
		*sd++ = (0xC0);
		*sd++ = (0xFF);
		*sd++ = (0xEE);
		*sd++ = (movieIndex);

		*sd++ = (auto_play_mask >> 8);
		*sd++ = (auto_play_mask & 0xFF);
		*sd++ = (auto_play_delay >> 8);
		*sd++ = (auto_play_delay & 0xFF);
		*sd++ = (0);
		*sd++ = (auto_play_loop);		
		for (i = 0; i < movieIndex; i++)
		{
			unsigned short hiword = (movieStartSector[i] >> 16);
			unsigned short loword = (movieStartSector[i] & 0xFFFF);
			*sd++ = hiword >> 8;
			*sd++ = hiword & 0xFF;
			*sd++ = loword >> 8;
			*sd++ = loword & 0xFF;
		}
		rewind(fout);
		fwrite(sector, 1, sd - sector, fout);
	}
	if (dfd != NULL) closedir(dfd);
	if (fout != NULL) fclose(fout);
	if (fin != NULL) fclose(fin);
	if (cmdf != NULL) pclose(cmdf);
	if (filenames != NULL)
	{
		unsigned i;
		for (i = 0; i < numFiles; i++)
		{
			if (filenames[i] != NULL)
				free(filenames[i]);
		}
		free(filenames);
	}
	if (tmp_dirname != NULL)
	{
		nftw(tmp_dirname, remove_callback, FOPEN_MAX, FTW_DEPTH | FTW_MOUNT | FTW_PHYS);
	}

	// check sector alignment
	{
		struct stat st;
		stat(outname, &st);
		printf("%s size: %ld\n", outname, (long)st.st_size);
	}
    return retcode;
}
