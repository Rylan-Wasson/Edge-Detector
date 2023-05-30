#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include <pthread.h>
#include <string.h>

#define LAPLACIAN_THREADS 4     //change the number of threads as you run your concurrency experiment

/* Laplacian filter is 3 by 3 */
#define FILTER_WIDTH 3       
#define FILTER_HEIGHT 3      

#define LINE_SIZE 256
#define RGB_COMPONENT_COLOR 255
#define FILE_NUM_INDEX 9 // index to update output filename num

int truncateColorValue(int value); // helper method prototype
pthread_mutex_t lock;


typedef struct {
      unsigned char r, g, b;
} PPMPixel;

struct parameter {
    PPMPixel *image;         //original image pixel data
    PPMPixel *result;        //filtered image pixel data
    unsigned long int w;     //width of image
    unsigned long int h;     //height of image
    unsigned long int start; //starting point of work
    unsigned long int size;  //equal share of work (almost equal if odd)
};


struct file_name_args {
    char *input_file_name;      //e.g., file1.ppm
    char output_file_name[20];  //will take the form laplaciani.ppm, e.g., laplacian1.ppm
};


/*The total_elapsed_time is the total time taken by all threads 
to compute the edge detection of all input images .
*/
double total_elapsed_time = 0; 

/*This is the thread function. It will compute the new values for the region of image specified in params (start to start+size) using convolution.
    For each pixel in the input image, the filter is conceptually placed on top ofthe image with its origin lying on that pixel.
    The  values  of  each  input  image  pixel  under  the  mask  are  multiplied  by the corresponding filter values.
    Truncate values smaller than zero to zero and larger than 255 to 255.
    The results are summed together to yield a single output value that is placed in the output image at the location of the pixel being processed on the input.
 
 */
void *compute_laplacian_threadfn(void *params)
{
    
    int laplacian[FILTER_WIDTH][FILTER_HEIGHT] =
    {
        {-1, -1, -1},
        {-1,  8, -1},
        {-1, -1, -1}
    };

    int red, green, blue;
    struct parameter *parameters = (struct parameter *)params;
    int num_pixels = (parameters->h * parameters->w);
    // iterate through pixel data
    for(int i = parameters->start; i < (parameters->start + parameters->size); i++){
        red = 0;
        green = 0;
        blue = 0;

        // current coordinates of pixel
        int pixel_x = i % parameters->w;
        int pixel_y = i / parameters->w;
        // iterate through filter width
        for(int fw = 0; fw < FILTER_WIDTH; fw++){
            // iterate through filter height
            for(int fh = 0; fh < FILTER_HEIGHT; fh++){
                int x = (pixel_x - FILTER_WIDTH / 2 + fw + parameters->w) % parameters->w;
                int y = (pixel_y - FILTER_HEIGHT / 2 + fh + parameters->h) % parameters->h;

                red += (parameters->image[y * parameters->w + x].r * laplacian[fw][fh]);
                green += (parameters->image[y * parameters->w + x].g * laplacian[fw][fh]);
                blue += (parameters->image[y * parameters->w + x].b * laplacian[fw][fh]);
            } // filter height
        } // filter width
        parameters->result[pixel_y * parameters->w + pixel_x].r = truncateColorValue(red);
        parameters->result[pixel_y * parameters->w + pixel_x].g = truncateColorValue(green);
        parameters->result[pixel_y * parameters->w + pixel_x].b = truncateColorValue(blue);
    } // pixel data
    return NULL;
}

/* Apply the Laplacian filter to an image using threads.
 Each thread shall do an equal share of the work, i.e. work=height/number of threads. If the size is not even, the last thread shall take the rest of the work.
 Compute the elapsed time and store it in *elapsedTime (Read about gettimeofday).
 Return: result (filtered image)
 */
PPMPixel *apply_filters(PPMPixel *image, unsigned long w, unsigned long h, double *elapsedTime) {
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);
    PPMPixel *result = malloc((w * h) * sizeof(PPMPixel));
    pthread_t threads[LAPLACIAN_THREADS];
    struct parameter** params = malloc(LAPLACIAN_THREADS * sizeof(struct parameter*));
    
    // initialize threads
    for(int i = 0; i < LAPLACIAN_THREADS; i++){
        params[i] = malloc(sizeof(struct parameter));
        params[i]->w = w;
        params[i]->h = h;
        params[i]->image = image;
        params[i]->result = result;
        params[i]->size = (w * h) / LAPLACIAN_THREADS;
        params[i]->start = (params[i]->size * i);
        if(i == (LAPLACIAN_THREADS - 1)){ // check for any remainder on last iteration
            params[i]->size += (params[i]->size % LAPLACIAN_THREADS); 
        }
        pthread_mutex_lock(&lock);
        if(pthread_create(&threads[i], NULL, compute_laplacian_threadfn, (void*)params[i]) != 0){
            printf("Error: Cannot create thread");
            exit(1);
        }
        pthread_mutex_unlock(&lock);
    }

    // join threads
    for(int j = 0; j < LAPLACIAN_THREADS; j++){
        if(pthread_join(threads[j], NULL)){
            printf("Error: Cannot join threads");
            exit(1);
        }
        free(params[j]);
    }
    
    free(params);
    gettimeofday(&end_time, NULL);
    long seconds = end_time.tv_sec - start_time.tv_sec;
    long microseconds = end_time.tv_usec - start_time.tv_usec;
    *elapsedTime = seconds + microseconds / 1000000.0;
    return result;
}


/*Create a new P6 file to save the filtered image in. Write the header block
 e.g. P6
      Width Height
      Max color value
 then write the image data.
 The name of the new file shall be "filename" (the second argument).
 */
void write_image(PPMPixel *image, char *filename, unsigned long int width, unsigned long int height)
{
    // open a new file
    FILE *fp = fopen(filename, "wb"); 
    if(fp == NULL){
        perror("Error creating file");
        exit(1);
    }

    // write header
    fprintf(fp, "P6\n%lu %lu\n255\n", width, height);
    
    // write pixel data
    int num_pixels = (width * height);
    for(int i = 0; i < num_pixels; i++){
        PPMPixel pixel = image[i];
        fwrite(&pixel, sizeof(PPMPixel), 1, fp);
    }
    fclose(fp);
}



/* Open the filename image for reading, and parse it.
    Example of a ppm header:    //http://netpbm.sourceforge.net/doc/ppm.html
    P6                  -- image format
    # comment           -- comment lines begin with
    ## another comment  -- any number of comment lines
    200 300             -- image width & height 
    255                 -- max color value
 
 Check if the image format is P6. If not, print invalid format error message.
 If there are comments in the file, skip them. You may assume that comments exist only in the header block.
 Read the image size information and store them in width and height.
 Check the rgb component, if not 255, display error message.
 Return: pointer to PPMPixel that has the pixel data of the input image (filename).The pixel data is stored in scanline order from left to right (up to bottom) in 3-byte chunks (r g b values for each pixel) encoded as binary numbers.
 */
PPMPixel *read_image(const char *filename, unsigned long int *width, unsigned long int *height)
{

    PPMPixel *img;
    FILE* fp = fopen(filename, "r");
    if(fp == NULL){
        perror("Error opening file");
        exit(1);
    } 
    // grab first line
    char *line = malloc(LINE_SIZE);
    fgets(line, LINE_SIZE, fp);

    // confirm that this is a p6 file
    if(strcmp(line, "P6\n") != 0){
        perror("Incorrect file format");
        exit(1);
    } 

    int num_lines = 0;
    // grab and store dimensions, confirm rgb component
    while(num_lines < 2 && line != NULL){
        fgets(line, LINE_SIZE, fp);
        // ignore comments
        if(line[0] != '#'){
            // dimensions
            if(num_lines == 0){
                char *dimension = strtok(line, " "); 
                sscanf(dimension, "%lu", width);
                
                dimension = strtok(NULL, " ");
                sscanf(dimension, "%lu", height);
            // confirm rgb component
            } else if (num_lines == 1){
                size_t len = strlen(line);
                line[len-1] = '\0';

                if(strcmp(line, "255") != 0){
                    perror("RGB error");
                    exit(1);
                }
            }
            num_lines++;
        }
    }

    unsigned long num_pixels = (*width * *height); // number of pixels in image
    int num_bytes = (sizeof(PPMPixel) * num_pixels); // number of bytes required to store pixels
    
    img = malloc(num_bytes); // TODO free me
    unsigned char color;
    
    // create pixels from file, store in img
    for(int i = 0; i < num_pixels; i++){
        PPMPixel pixel;
        fread(&color, 1, 1, fp); // r
        pixel.r = color;

        fread(&color, 1, 1, fp); // g
        pixel.g = color;

        fread(&color, 1, 1, fp); // b
        pixel.b = color;

        img[i] = pixel;
    }
    free(line);
    fclose(fp);
    return img;
}

/* The thread function that manages an image file. 
 Read an image file that is passed as an argument at runtime. 
 Apply the Laplacian filter. 
 Update the value of total_elapsed_time.
 Save the result image in a file called laplaciani.ppm, where i is the image file order in the passed arguments.
 Example: the result image of the file passed third during the input shall be called "laplacian3.ppm".

*/
void *manage_image_file(void *args){
    struct file_name_args *file_args = (struct file_name_args *)args;
    
    unsigned long int w = 0;
    unsigned long int h = 0;
    pthread_mutex_lock(&lock);
    
    PPMPixel *img = read_image(file_args->input_file_name, &w, &h); // read image
    pthread_mutex_unlock(&lock);

    double elapsed_time = 0;
    PPMPixel *result = apply_filters(img,w,h,&elapsed_time); // apply filter to input image

    pthread_mutex_lock(&lock);
    total_elapsed_time += elapsed_time;
    write_image(result,file_args->output_file_name,w,h); // write filtered image to output file
    free(img);
    free(result);
    free(args);
    pthread_mutex_unlock(&lock);
}

/* Helper Methods */


/* Truncate a colors value to be in range of 0 - 255*/
int truncateColorValue(int value){
    if(value > RGB_COMPONENT_COLOR){
        return RGB_COMPONENT_COLOR;
    } else if (value < 0){
        return 0;
    } else {
        return value;
    }
}


/*The driver of the program. Check for the correct number of arguments. If wrong print the message: "Usage ./a.out filename[s]"
  It shall accept n filenames as arguments, separated by whitespace, e.g., ./a.out file1.ppm file2.ppm    file3.ppm
  It will create a thread for each input file to manage.  
  It will print the total elapsed time in .4 precision seconds(e.g., 0.1234 s). 
 */
int main(int argc, char *argv[])
{
    if(argc < 2){
        printf("Usage ./edge_detector filename[s]");
        exit(1);
    }
    
    int num_files = argc - 1;
    pthread_t threads[num_files];
    struct file_name_args** args = malloc(num_files * sizeof(struct file_name_args*));
    pthread_mutex_init(&lock, NULL);
    // iterate through files, creating a thread for each and managing the relevant file
    for(int i = 1; i < num_files+1; i++){
        args[i-1] = malloc(sizeof(struct file_name_args));
        args[i-1]->input_file_name = argv[i];
        sprintf(args[i-1]->output_file_name, "laplacian%d.ppm", i);
        pthread_mutex_lock(&lock);
        // create thread, passing file args into manage image file
        if(pthread_create(&threads[i-1], NULL, manage_image_file, args[i-1]) != 0){ 
            printf("Error: Cannot create thread");
            exit(1);
        }
        pthread_mutex_unlock(&lock);
    }

    // join threads
    for(int j = 0; j < num_files; j++){
        if(pthread_join(threads[j], NULL)){
            printf("Error: Cannot join threads");
            exit(1);
        }
    }
    
    free(args);
    pthread_mutex_destroy(&lock);
    printf("Total Elapsed Time: %f\n", total_elapsed_time);
    return 0;
}

