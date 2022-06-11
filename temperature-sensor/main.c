#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

#define N_ANALOG_VALUES 800

uint16_t adc_values_raw[N_ANALOG_VALUES];

struct ADC{
    uint16_t value_digital;
    float value_volt;
};


const float adc_sample_frequency = 0.1;
const int post_adc_data_frequnecy = 120;

void *readADCTimer(void *vargp) {
    while(1){
        char s[] = "test";
        puts(s);
        sleep(adc_sample_frequency);
    }
       // return NULL;
}

void *pubToPost(void *vargp){
    while(1){
        puts("post");
        sleep(post_adc_data_frequnecy);
    }
}

void getAllTempFromFile();


int main()
{
    struct ADC adc;
    getAllTempFromFile();

    pthread_t thread_id_adc;
    pthread_create(&thread_id_adc, NULL, readADCTimer, NULL);

    pthread_t thread_id_POST;
    pthread_create(&thread_id_POST, NULL, pubToPost, NULL);



    while(1){}
    return 0;
}


void getAllTempFromFile(){

    char line[5];
    FILE *fileptr = fopen("temperature.txt", "r");

    ssize_t read;

    if(fileptr == NULL){
        printf("Error reading temperature.txt");
    }

    int i = 0;
    while(fgets(line, 10, fileptr) != NULL && i < N_ANALOG_VALUES) {


        adc_values_raw[i] = atoi(line);
        printf("%i\n", adc_values_raw[i]);
        i++;
    }

}
