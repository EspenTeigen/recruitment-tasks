#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <json-c/json.h>


//-----------ADC related variables and functions-----------------

//Lines of values in temperature.txt
#define N_ANALOG_VALUES 766
//Sleep value in thread readADCTimer to sample data every 100ms(100 000us)
const float adc_sample_frequency = 100000;


//Used to store all the values from file to simulate getting data from
//adc
uint16_t adc_values_from_file[N_ANALOG_VALUES];
uint8_t adc_ready = 0x00;
uint16_t single_value_from_adc;

struct ADC{
    float value_volt;
};

//used to lock thread so values cannot be changed during data retrieval
static pthread_mutex_t adc_lock;
//Start thread and mutex lock
int initreadADCTimer();
//samples single value from array of samples every 100ms
void *readADCTimer(void *vargp);
void converToCelsius();
//Read adc values from temperature.txt and stores it in adc_values_from_file
void getAllTempFromFile();


//----------------POST request variables and functions-----------------

//Sleep value in thread pubToPOST to send data every two minutes
const int post_adc_data_frequnecy = 1;

typedef struct TemperatureMeasurement{
        char start[20];
        char end[20];
        float min;
        float max;
        float average;
    }temperatureMeasurement;

void initpubToPOSTTimer();
void *pubToPOST(void * vargp);
void createJSON(temperatureMeasurement * measurement, json_object * object);

//---------------Main----------------------------

int main()
{
    temperatureMeasurement ms;
    strcpy(ms.start, "starting");
    strcpy(ms.end, "stopping");
    ms.average = 25.0;
    ms.min = -50.3;
    ms.max = 43.1;

    json_object *object;
    createJSON(&ms, object);
  


    getAllTempFromFile();

    initreadADCTimer();
    initpubToPOSTTimer();
   
    while(1){
        if(adc_ready){
            converToCelsius();
            adc_ready = 0x00;
        }
    }
    return 0;
}

void createJSON(temperatureMeasurement * measurement, json_object * object){
    char buffer[1024];
    sprintf(buffer, "{\"time\": { \"start\": \"%s\",  \
	            	              \"end\": \"%s\"}, \
	                    \"min\": \"%f\", \
	                    \"max\": \"%f\",  \
	                    \"average\": \"%f\"}" \
                , measurement->start, measurement->end, measurement->min, measurement->max, measurement->average);

    json_object *root = json_tokener_parse(buffer);
    printf("%lu", sizeof(root));
    

}

int initreadADCTimer(){
    //Create mutex to block write access
    if (pthread_mutex_init(&adc_lock, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }

    //Thread to read 
    pthread_t thread_id_adc;
    pthread_create(&thread_id_adc, NULL, readADCTimer, NULL);
    return 0;
}

void initpubToPOSTTimer(){
    pthread_t thread_id_POST;
    pthread_create(&thread_id_POST, NULL, pubToPOST, NULL);
}

void getAllTempFromFile(){

    char line[5];
    FILE *fileptr = fopen("temperature.txt", "r");
    ssize_t read;

    if(fileptr == NULL){
        printf("Error reading temperature.txt");
    }
    //Retrive data line by line, convert to int and save to arrray
    int i = 0;
    while(fgets(line, 10, fileptr) != NULL && i < N_ANALOG_VALUES) {
        adc_values_from_file[i] = atoi(line);
        i++;
    }
}



//Triggers the adc_ready flag and stores value everytime a "sample" is ready
void *readADCTimer(void *vargp) {

    int i = 0;
    uint8_t samples_available = 0x01;

    while(samples_available){
        
        //The adc_ready_flag is used to reduce time used in thread, and improve determinism of
        //sample collection time
        if(i < N_ANALOG_VALUES) {

            pthread_mutex_lock(&adc_lock);
            adc_ready = 0x01;
            single_value_from_adc = adc_values_from_file[i];
            i++;
            pthread_mutex_unlock(&adc_lock);
            //slow down runtime of thread to 10 
            usleep(adc_sample_frequency);
        }
        //If there is no more samples, exit thread
        else{
        samples_available = 0x00;
        } 
    }
    return NULL;
}

void converToCelsius(){
    //Conversion from 12-bit adc to celsius when f(0)=-50 and f(4096)=50. f(x) = 0.0244x -50 
    float temperature = 0.0244*(float)single_value_from_adc - 50.0;
    printf("%f\n", temperature);
}

void *pubToPOST(void *vargp){
    while(1){
        puts("post");
        sleep(post_adc_data_frequnecy);
    }
}