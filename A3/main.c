#include<time.h>
#include<stdio.h>
#include<stdlib.h>
#include<openacc.h>

//================================================
// ppmFile.h
//================================================
#include <sys/types.h>
typedef struct Image
{
  int width;
  int height;
  unsigned char *data;
} Image;


struct timespec start, stop;

Image *ImageCreate(int width, int height);
Image *ImageRead(char *filename);
void   ImageWrite(Image *image, char *filename);
int    ImageWidth(Image *image);
int    ImageHeight(Image *image);
void   ImageClear(Image *image, unsigned char red, unsigned char green, unsigned char blue);
void   ImageSetPixel(Image *image, int x, int y, int chan, unsigned char val);
unsigned char ImageGetPixel(Image *image, int x, int y, int chan);


//================================================
// The Blur Filter
//================================================
void ProcessImageACC(Image **data, int rad, Image **output)
{
   int max_x=(*data)->width, max_y=(*data)->height, min_x=0, min_y=0;
   printf("Processing Image with dimensions: %d x %d\n", max_x, max_y);
   int MaxX, MinX, MaxY, MinY;

   unsigned char* restrict in = (*data)->data;
   unsigned char* restrict out = (*output)->data;  //can also use Create() for newer systems
                                          //or use managed memory flag for combined CPU/GPU memory

   if(rad == 0){
	for(int i = 0; i < max_y; i++){
		for(int j = 0; j < max_x; j++){
			out[(i*max_x+j)*3] = in[(i*max_x+j)*3];
			out[(i*max_x+j)*3+1] = in[(i*max_x+j)*3+1];
			out[(i*max_x+j)*3+2] = in[(i*max_x+j)*3+2];
		}
	}	 
	return;
   }

#pragma acc data copyin(in[:max_x*max_y*3]) copyout(out[:max_x*max_y*3])
#pragma acc parallel loop //num_gangs(256)

   for(int i = 0; i < max_y; i++){ //row
    for(int j = 0; j < max_x; j++){ //col

    	MinX = j - rad < 0 ? 0 : j - rad;
    	MaxX = j + rad > max_x ? max_x : j+rad;
    	MaxY = i + rad > max_y ? max_y : i+rad; 
    	MinY = i - rad < 0 ? 0 : i - rad;

      double sumR = 0.0, sumG = 0.0, sumB = 0.0;
      for(int k = MinY; k < MaxY; k++){
		    for(int l = MinX; l < MaxX; l++){
			   sumR += in[(k*max_x+l)*3];
			   sumG += in[(k*max_x+l)*3+1];
			   sumB += in[(k*max_x+l)*3+2];
		    }
	    }
			sumR /= (MaxX-MinX+1)*(MaxY-MinY+1);
			sumG /= (MaxX-MinX+1)*(MaxY-MinY+1);
			sumB /= (MaxX-MinX+1)*(MaxY-MinY+1);
      out[(i*max_x+j)*3] = sumR;
      out[(i*max_x+j)*3+1] = sumG;
      out[(i*max_x+j)*3+2] = sumB;
    }
  }
}

//================================================
// Main Program
//================================================
int main(int argc, char* argv[]){
    //vars used for processing:
    Image *data, *result;
    int dataSize;
    int filterRadius = atoi(argv[1]);

    //===read the data===
    data = ImageRead(argv[2]);

    //===send data to nodes===
    //send data size in bytes
    dataSize = sizeof(unsigned char)*data->width*data->height*3;

    //===process the image===
    //allocate space to store result
    result = (Image*)malloc(sizeof(Image));
    result->data = (unsigned char*)malloc(dataSize);
    result->width = data->width;
    result->height = data->height;

    //initialize all to 0
    for(int i = 0; i < (result->width*result->height*3); i++){
        result->data[i] = 0;
    }

    //apply teh filter
    clock_gettime(CLOCK_MONOTONIC, &start);
    ProcessImageACC(&data, filterRadius, &result);
    clock_gettime(CLOCK_MONOTONIC, &stop);
    //===save the data back===
    ImageWrite(result, argv[3]);
    double diff = stop.tv_sec*1000000000 + stop.tv_nsec - (start.tv_sec*1000000000 + start.tv_nsec);
    printf("%lf\n", diff/1000000000);

    return 0;
}


//================================================
// ppmFile.c
//================================================
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
/************************ private functions ****************************/
/* die gracelessly */
static void die(char *message){
  fprintf(stderr, "ppm: %s\n", message);
  exit(1);
}
/* check a dimension (width or height) from the image file for reasonability */
static void checkDimension(int dim){
  if (dim < 1 || dim > 4000) 
    die("file contained unreasonable width or height");
}
/* read a header: verify format and get width and height */
static void readPPMHeader(FILE *fp, int *width, int *height){
  char ch;
  int  maxval;
  if (fscanf(fp, "P%c\n", &ch) != 1 || ch != '6') 
    die("file is not in ppm raw format; cannot read");
  /* skip comments */
  ch = getc(fp);
  while (ch == '#')
    {
      do {
	ch = getc(fp);
      } while (ch != '\n');	/* read to the end of the line */
      ch = getc(fp);            /* thanks, Elliot */
    }
  if (!isdigit(ch)) die("cannot read header information from ppm file");
  ungetc(ch, fp);		/* put that digit back */
  /* read the width, height, and maximum value for a pixel */
  fscanf(fp, "%d%d%d\n", width, height, &maxval);
  if (maxval != 255) die("image is not true-color (24 bit); read failed");
  checkDimension(*width);
  checkDimension(*height);
}

Image *ImageCreate(int width, int height){
  Image *image = (Image *) malloc(sizeof(Image));
  if (!image) die("cannot allocate memory for new image");
  image->width  = width;
  image->height = height;
  image->data   = (unsigned char *) malloc(width * height * 3);
  if (!image->data) die("cannot allocate memory for new image");
  return image;
}
Image *ImageRead(char *filename){
  int width, height, num, size;
  unsigned  *p;
  Image *image = (Image *) malloc(sizeof(Image));
  FILE  *fp    = fopen(filename, "r");
  if (!image) die("cannot allocate memory for new image");
  if (!fp)    die("cannot open file for reading");
  readPPMHeader(fp, &width, &height);
  size          = width * height * 3;
  image->data   = (unsigned  char*) malloc(size);
  image->width  = width;
  image->height = height;
  if (!image->data) die("cannot allocate memory for new image");
  num = fread((void *) image->data, 1, (size_t) size, fp);
  if (num != size) die("cannot read image data from file");
  fclose(fp);
  return image;
}
void ImageWrite(Image *image, char *filename){
  int num;
  int size = image->width * image->height * 3;
  FILE *fp = fopen(filename, "w");
  if (!fp) die("cannot open file for writing");
  fprintf(fp, "P6\n%d %d\n%d\n", image->width, image->height, 255);
  num = fwrite((void *) image->data, 1, (size_t) size, fp);
  if (num != size) die("cannot write image data to file");
  fclose(fp);
} 
int ImageWidth(Image *image){
  return image->width;
}
int ImageHeight(Image *image){
  return image->height;
}
void ImageClear(Image *image, unsigned char red, unsigned char green, unsigned char blue){
  int i;
  int pix = image->width * image->height;
  unsigned char *data = image->data;
  for (i = 0; i < pix; i++)
    {
      *data++ = red;
      *data++ = green;
      *data++ = blue;
    }
}
#pragma acc routine seq
void ImageSetPixel(Image *image, int x, int y, int chan, unsigned char val){
  int offset = (y * image->width + x) * 3 + chan;
  image->data[offset] = val;
}
#pragma acc routine seq
unsigned  char ImageGetPixel(Image *image, int x, int y, int chan){
  int offset = (y * image->width + x) * 3 + chan;
  return image->data[offset];
}
