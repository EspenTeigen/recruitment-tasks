#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include <curl/curl.h>

const char * POST_URL = "http://localhost:5000/api/temperature";
const char * FALLBACK_URL = "http://localhost:5000/api/missing";


typedef struct TemperatureMeasurement{
        char start[20];
        int sizeOfStart;       
        char end[20];
        int sizeOfEnd;
        float min;
        float max;
        float average;
        float sum;
        int number_of_measurements;
    }temperatureMeasurement;



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


//used to lock thread so values cannot be changed during data retrieval
static pthread_mutex_t adc_lock;
//Start thread and mutex lock
int initReadADCTimer();
//samples single value from array of samples every 100ms
void *readADCTimer(void *vargp);
void convertToCelsius(temperatureMeasurement * measurement);
//Read adc values from temperature.txt and stores it in adc_values_from_file
void getAllADCTemperatureFromFile();


//----------------POST request variables and functions-----------------

//Sleep value in thread pubToPOST to send data every two minutes
const int post_adc_data_frequency = 3;
//Toggled by pubToPOST thread every two minutes to signal that is
//time to send data
uint8_t sendDataPOST = 0x00;
//Lock during work on shared memory in thread
static pthread_mutex_t post_lock;


int initpubToPOSTTimer();
void *pubToPOST(void * vargp);
void createJSON(temperatureMeasurement * measurement, char * string, int size_of_string);
void getDateTimeISO8601(char * dateTime, int sizeDateTime);
int POSTMeasurement(char * measurement_json, int size);


//---------------Main----------------------------

int main()
{

    temperatureMeasurement tm;
    tm.number_of_measurements = 0;
    tm.sizeOfStart = sizeof(tm.start);
    tm.sizeOfEnd = sizeof(tm.end);

    getAllADCTemperatureFromFile();

    initReadADCTimer();

    initpubToPOSTTimer();
   
    while(1){
        //Get data everytime adc is ready
        if(adc_ready){
            convertToCelsius(&tm);
            adc_ready = 0x00;
        }
        if(sendDataPOST){

            
            //Store local copy so things dont change before data is sent
            temperatureMeasurement tm_copy = tm;

            //reset number_of_measurements so everything will be reset in convertToCelsius()
            tm.number_of_measurements = 0;

            //Store end time
            getDateTimeISO8601(tm_copy.end, tm_copy.sizeOfEnd);

            //calculate average
            tm_copy.average = tm_copy.sum / tm_copy.number_of_measurements;

            //Reset flag that signals it is time to send data
            sendDataPOST = 0x00;

            //construct json
            char measurement_json[2048];
            createJSON(&tm_copy, measurement_json, sizeof(measurement_json));
            //printf("%s\n", measurement_json);

            POSTMeasurement(measurement_json, sizeof(measurement_json));
            
        }
    }
    return 0;
}


void createJSON(temperatureMeasurement * measurement, char * string, int size_of_string){
    char buffer[size_of_string];

    //sprintf(buffer, "{\"time\": { \"start\": \"%s\",  \
	//            	              \"end\": \"%s\"}, \
	//                    \"min\": \"%.2f\", \
	//                    \"max\": \"%.2f\",  \
	//                    \"average\": \"%.2f\"}"      
    //            ,measurement->start, measurement->end, measurement->min, measurement->max, measurement->average);

    sprintf(buffer, "{\n	\"time\": {\n		\"start\": \"%s\", \n		\"end\": \"%s\" \n	},\n	\"min\": %.2f, \n	\"max\": %.2f, \n	\"avg\": %.2f\n}",
            measurement->start, measurement->end, measurement->min, measurement->max, measurement->average);


    memcpy(string, buffer, size_of_string);

    printf("%s\n", string);
   
}

int initReadADCTimer(){
    //Create mutex to block write access
    if (pthread_mutex_init(&adc_lock, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }

    //Thread to read adc
    pthread_t thread_id_adc;
    pthread_create(&thread_id_adc, NULL, readADCTimer, NULL);
    return 0;
}

int initpubToPOSTTimer(){

    //Create mutex to block write access
    if (pthread_mutex_init(&post_lock, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }
    pthread_t thread_id_POST;
    pthread_create(&thread_id_POST, NULL, pubToPOST, NULL);
    return 0;
}

void getAllADCTemperatureFromFile(){

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
    fclose(fileptr);
}



//Thread to read samples from ADC
void *readADCTimer(void *vargp) {

    int i = 0;
    //Runs through all samples and roll over to beginning when end is reach
    //length of array is hardcoded since this will not change
    //In a situation I would use an interrupt vector to detect when ADC is ready
    while(1){
        //The adc_ready_flag is used to reduce time used in thread, and improve determinism of
        //sample collection time since time used to convert data does not add to the cycle time
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
        //samples_available = 0x00;
        i = 0;
        } 
    }
    return NULL;
}

void convertToCelsius(temperatureMeasurement * measurement){
    //Conversion from 12-bit adc to celsius when f(0)=-50 and f(4096)=50. f(x) = 0.0244x -50 
    float temperature = 0.0244*(float)single_value_from_adc - 50.0;

    //Reset all values when data has been read, and number_of_measurements are set to zero
    if(measurement->number_of_measurements == 0){
        measurement->sum = 0;
        measurement->average = 0;
        measurement->max = temperature;
        measurement->min = temperature;
        //Save dateTime from when we start to sample data
        getDateTimeISO8601(measurement->start, measurement->sizeOfStart);
    }

    measurement->sum += temperature;
    measurement->number_of_measurements++;
    if(temperature > measurement->max) measurement->max = temperature;
    if(temperature < measurement->min) measurement->min = temperature;

}


void *pubToPOST(void *vargp){
    while(1){
        pthread_mutex_lock(&post_lock);
        sendDataPOST = 0x01;
        sleep(post_adc_data_frequency);
        pthread_mutex_unlock(&post_lock);
    }
}

void getDateTimeISO8601(char * dateTime, int sizeDateTime){
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);

    sprintf(dateTime, "%i-%i-%i", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    char buffer[sizeDateTime];
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S.000%z", tm);
    memcpy(dateTime, buffer, sizeof(buffer));
}


int POSTMeasurement(char * measurement_json, int size){

    CURL *curl;
    CURLcode res;
    char error_buf[CURL_ERROR_SIZE];
    curl = curl_easy_init();
    if(curl) {
        //tell curl that we want to post
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
        //URL to server and api
        curl_easy_setopt(curl, CURLOPT_URL, POST_URL);
        //Turn on error feedback so we can catch the 500 error
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buf);

        //curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https");
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, measurement_json);
        res = curl_easy_perform(curl);
        
        //Check for error 500, return error if present
        if(res == CURLE_HTTP_RETURNED_ERROR && strstr(error_buf, "500")){
            printf("%s\n", error_buf);
            return 1;
        }
    }
    curl_easy_cleanup(curl);
    return 0;
}