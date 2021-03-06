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

#define MAX_JSON_STRING_SIZE 150     
#define N_OLD_JSON_STRINGS 10       //Maximum of failed JSON-strings
#define N_ANALOG_VALUES 766         //Lines of values in temperature.txt
#define INTERVAL_TO_POST_REQUEST_SEC 120 //Time in seconds between POST requests

//Where all values related to ADC is stored
typedef struct TemperatureMeasurement{
        char start[80];
        int sizeOfStart;       
        char end[80];
        int sizeOfEnd;
        float min;
        float max;
        float average;
        float sum;
        int number_of_measurements;
    }temperatureMeasurement;

//-----------ADC related variables and functions-----------------


//Sleep value in thread readADCTimer to sample data every 100ms(100 000us)
const float ADC_SAMPLE_FREQUENCY =  100000;
const int NUMBER_OF_SAMPLES_IN_INTERVAL = (ADC_SAMPLE_FREQUENCY / 10000) * INTERVAL_TO_POST_REQUEST_SEC; // How many samples there should be between POST request


//Used to store all the values from file to simulate getting data from
//adc
uint16_t adc_values_from_file[N_ANALOG_VALUES];
uint8_t adc_ready = 0x00;
uint16_t single_value_from_adc;

static pthread_mutex_t adc_lock; //used to lock thread so values cannot be changed during data retrieval
int initReadADCTimer(char * filename);  //Start thread and mutex lock
void *readADCTimer(void *vargp); //samples single value from array of samples every 100ms
void convertToCelsius(temperatureMeasurement * measurement);
void getAllADCTemperatureFromFile(char * filename); //Read adc values from temperature.txt and stores it in adc_values_from_file


//----------------POST request variables and functions-----------------


//Toggled by pubToPOST thread every two minutes to signal that is
//time to send data
uint8_t sendDataPOST = 0x00;
static pthread_mutex_t post_lock; //Lock during work on shared memory in thread


int initpubToPOSTTimer(); //Starts thread and mutex lock (probably overkill for this)
void *pubToPOST(void * vargp); //Thread that triggers the sendDataPOST flag every two minuts
void createJSON(temperatureMeasurement * measurement, char * string, int size_of_string);
void getDateTimeISO8601(char * dateTime, int sizeDateTime);
int POSTMeasurement(const char * URL, char * measurement_json); //Function that uses libcurl to create POST request and read error from server


typedef struct PreviousJsonString{//used to store previous json strings that failed to be sent, number of errors and elementrs is stored
    char previous_json_string[N_OLD_JSON_STRINGS][MAX_JSON_STRING_SIZE]; 
    uint8_t N_errors;
    uint8_t N_elements;
}previousJsonString;



//---------------Main----------------------------

int main()
{
    //----------------Initialization-------------
    //Initialize struct to store ADC data
    temperatureMeasurement tempMeas;
    tempMeas.number_of_measurements = 0;
    tempMeas.sizeOfStart = sizeof(tempMeas.start);
    tempMeas.sizeOfEnd = sizeof(tempMeas.end);

    //Struct to keep old measurements and info about sending status
    previousJsonString failedAttempts;
    failedAttempts.N_errors= 0;
    failedAttempts.N_elements = 0x00;

    initReadADCTimer("temperature.txt");
    initpubToPOSTTimer();
   
    //------------Main loop-----------------------------

    while(1){

        //Get data everytime adc is ready
        if(adc_ready){
            convertToCelsius(&tempMeas);
            adc_ready = 0x00;
        }

        //Check if ready to post and that there is enough samples that the
        //average is similar even when we boot up the system
        if(sendDataPOST && tempMeas.number_of_measurements >= NUMBER_OF_SAMPLES_IN_INTERVAL){

            sendDataPOST = 0x00; //Reset flag that signals it is time to send data
            
            temperatureMeasurement tm_copy = tempMeas; //Store local copy so things dont change before data is sent (this is overkill for a slow system ) 

            tempMeas.number_of_measurements = 0; //reset number_of_measurements so everything will be reset in convertToCelsius()

            getDateTimeISO8601(tm_copy.end, tm_copy.sizeOfEnd);

            tm_copy.average = tm_copy.sum / tm_copy.number_of_measurements; //calculate average of temperature
            
            //construct json
            char measurement_json[MAX_JSON_STRING_SIZE];  
            createJSON(&tm_copy, measurement_json, MAX_JSON_STRING_SIZE);
        
            //This could probably be done more elegant by putting it in a textfile
            static const char * POST_URL = "http://localhost:5000/api/temperature";
            static const char * FALLBACK_URL = "http://localhost:5000/api/temperature/missing";            
            
            //Send data, if it fails, store value and send on the next 
            //two minute interval
            if(POSTMeasurement(POST_URL ,measurement_json)){
               //start to fill up array with old values, increment amount of elements in array       
                strcpy(failedAttempts.previous_json_string[failedAttempts.N_elements], measurement_json);
                failedAttempts.N_elements++;

            }

            //When POST request has failed 10 times, send data to /missing, and empty array
            if(failedAttempts.N_elements >= 10) {

                char failed_measurement_combined[10*MAX_JSON_STRING_SIZE];
                int i;
                //Combine all failed attempts
                for(i = 0; i < failedAttempts.N_elements; i++){
        
                    strcat(failed_measurement_combined, failedAttempts.previous_json_string[i]);
                    strcat(failed_measurement_combined, "\n");
    
                }

                failedAttempts.N_elements = 0;  //Since strings in array is removed, set counter to zero
                POSTMeasurement(FALLBACK_URL, failed_measurement_combined);  //Send combined string to fallback server
                
            }
        }
    }
    return 0;
}


//Takes the calculations that has been done on adc values and converts it to a raw json string
void createJSON(temperatureMeasurement * measurement, char * string, int size_of_string){
    
    char buffer[size_of_string];
    //Format data to json raw string, probably more elegant to keep json string in textfile
    //and use a json library to parse it, but that is not a very embedded thing to do
    sprintf(buffer, "{\n\"time\":{\n\t\"start\" : \"%s\", \n\t\"end\" : \"%s\" \n\t},\n	\"min\" : %.2f, \n\t\"max\" : %.2f,\t\n	\"avg\" : %.2f\n}",
            measurement->start, measurement->end, measurement->min, measurement->max, measurement->average);

    memcpy(string, buffer, size_of_string);
   
}


//Init thread that control when adc sample is ready
int initReadADCTimer(char * filename){
    //Create mutex to block write access
    if (pthread_mutex_init(&adc_lock, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }
    //Thread to read adc
    pthread_t thread_id_adc;
    pthread_create(&thread_id_adc, NULL, readADCTimer, filename);
    return 0;
}


//Init thread that control the time when a POST should be done
int initpubToPOSTTimer(){

    //Create mutex to block write access
    if (pthread_mutex_init(&post_lock, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }
    //Start post thread
    pthread_t thread_id_POST;
    pthread_create(&thread_id_POST, NULL, pubToPOST, NULL);
    return 0;
}


//import measurement values from file and store in global array
void getAllADCTemperatureFromFile(char * filename){

    char line[10];
    FILE *fileptr = fopen(filename, "r");
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
void *readADCTimer(void *filename) {

    int i = 0;
    //Read in data from file and place in adc_values_from_file
    getAllADCTemperatureFromFile((char *)filename);

    //Runs through all samples and roll over to beginning when end is reach
    //length of array is hardcoded since this will not change
    //In a real situation I would use an interrupt vector to detect when ADC is ready
    while(1){
        //The adc_ready_flag is used to reduce time used in thread, and improve determinism of
        //sample collection time since time used to convert data does not add to the cycle time
        //with this method. This is overkill for this system
        if(i < N_ANALOG_VALUES) {

            pthread_mutex_lock(&adc_lock);
            adc_ready = 0x01;
            single_value_from_adc = adc_values_from_file[i];
            i++;
            pthread_mutex_unlock(&adc_lock);
            //slow down runtime of thread to 10 
            usleep(ADC_SAMPLE_FREQUENCY);
        }
        else{
        //reset to start reading from beginning of array to simulate 
        //continuous datacollection from adc
        i = 0;
        } 
    }
    return NULL;
}



void convertToCelsius(temperatureMeasurement * measurement){

    float temperature = 0.0244*(float)single_value_from_adc - 50.0;  //Conversion from 12-bit adc to celsius when f(0)=-50 and f(4096)=50. f(x) = 0.0244x - 50 

    //Reset all values when data has been read, number_of_measurements are reset 
    //when post is triggered
    if(measurement->number_of_measurements == 0){
        measurement->sum = 0;
        measurement->average = 0;
        measurement->max = temperature;
        measurement->min = temperature;
        getDateTimeISO8601(measurement->start, measurement->sizeOfStart); //Save dateTime from when we start to sample data
    }
    
    //sum all values and amount of measurements, used to calculate average
    measurement->sum += temperature;
    measurement->number_of_measurements++;

    //Find max and min values
    if(temperature > measurement->max) measurement->max = temperature;
    if(temperature < measurement->min) measurement->min = temperature;

}


/*
---------------- Thread that sets bit to trigger POST in main thread
*/
void *pubToPOST(void *vargp){
    while(1){
        pthread_mutex_lock(&post_lock);
        //Rais flag to tell if test in main to post data
        sleep(INTERVAL_TO_POST_REQUEST_SEC);
        sendDataPOST = 0x01;
        pthread_mutex_unlock(&post_lock);
    }
}


//Convert date time to right format
void getDateTimeISO8601(char * dateTime, int sizeDateTime){

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char buffer[sizeDateTime];
    strftime(buffer, sizeDateTime, "%Y-%m-%dT%H:%M:%S", tm);
    strcpy(dateTime, buffer);
}



//Function to make a post request, takes a url * string and json string as input
int POSTMeasurement(const char * URL, char * measurement_json){

    CURL *curl;
    CURLcode res;
    char error_buf[CURL_ERROR_SIZE];
    curl = curl_easy_init();
    if(curl) {
            
        //========================Set options for libcurl===================================
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST"); //tell curl that we want to post
        curl_easy_setopt(curl, CURLOPT_URL, URL);  //URL to server and api
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);  //Turn on error feedback so we can catch the 500 error
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buf); //Get error from server
        curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https"); //We want to use http protocol

        //Include json header
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, measurement_json); //json string to be sent over https
            
        //=========================Send data and get response from server===============
        res = curl_easy_perform(curl);

        ///if it contains error 500, send err that fallback server needs to be used in next itteration
        //I do not check for other errors since i'm asked to only do error handling on 500
        if(strstr(error_buf, "500")){
            printf("\nCannot connect to server\n Error is: %s\n", error_buf);
            return 1;
        }
    }
    curl_easy_cleanup(curl);
    return 0;
}
