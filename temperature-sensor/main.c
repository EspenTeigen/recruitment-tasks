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
#define N_OLD_JSON_STRINGS 10

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

//Lines of values in temperature.txt
#define N_ANALOG_VALUES 766
//Sleep value in thread readADCTimer to sample data every 100ms(100 000us)
const float ADC_SAMPLE_FREQUENCY =  100000;


//Used to store all the values from file to simulate getting data from
//adc
uint16_t adc_values_from_file[N_ANALOG_VALUES];
uint8_t adc_ready = 0x00;
uint16_t single_value_from_adc;


//used to lock thread so values cannot be changed during data retrieval
static pthread_mutex_t adc_lock;
//Start thread and mutex lock
int initReadADCTimer(char * filename);
//samples single value from array of samples every 100ms
void *readADCTimer(void *vargp);
void convertToCelsius(temperatureMeasurement * measurement);
//Read adc values from temperature.txt and stores it in adc_values_from_file
void getAllADCTemperatureFromFile(char * filename);


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
int POSTMeasurement(const char * URL, char * measurement_json);

//used to store previous json strings that failed to be sent
typedef struct PreviousJsonString{
    //json string
    char previous_json_string[N_OLD_JSON_STRINGS][MAX_JSON_STRING_SIZE];
    //number of elements inn array
    uint8_t N_errors;
    uint8_t N_elements;
}previousJsonString;



//---------------Main----------------------------

int main()
{
    //----------------Initialization-------------
    //Variable to store ADC data
    temperatureMeasurement tempMeas;
    tempMeas.number_of_measurements = 0;
    tempMeas.sizeOfStart = sizeof(tempMeas.start);
    tempMeas.sizeOfEnd = sizeof(tempMeas.end);

    //Struct to keep old measurements and info about sending status
    previousJsonString prevJsonStr;
    prevJsonStr.N_errors= 0;
    prevJsonStr.N_elements = 0x00;

    initReadADCTimer("temperature.txt");
    initpubToPOSTTimer();
   
    //------------Main loop-----------------------------

    while(1){
        //Get data everytime adc is ready
        if(adc_ready){
            convertToCelsius(&tempMeas);
            adc_ready = 0x00;
        }

        //Check if ready to post and that there is enough samples to give a propper average
        if(sendDataPOST && tempMeas.number_of_measurements >= 10){
            
            //keep track on failing to send
            static uint8_t errSend = 0x00;
            //Store local copy so things dont change before data is sent
            temperatureMeasurement tm_copy = tempMeas;

            //reset number_of_measurements so everything will be reset in convertToCelsius()
            tempMeas.number_of_measurements = 0;

            //Store end time
            getDateTimeISO8601(tm_copy.end, tm_copy.sizeOfEnd);

            //calculate average
            tm_copy.average = tm_copy.sum / tm_copy.number_of_measurements;

            //Reset flag that signals it is time to send data
            sendDataPOST = 0x00;

            //construct json
            char measurement_json[MAX_JSON_STRING_SIZE];
            createJSON(&tm_copy, measurement_json, MAX_JSON_STRING_SIZE);
        
            //This could probably be done more elegant by putting it in a textfile
            static const char * POST_URL = "http://localhost:5000/api/temperature";
            static const char * FALLBACK_URL = "http://localhost:5000/api/temperature/missing";            

            //Check if POST failed last time, if it has use fallback server
            if(prevJsonStr.N_elements > 0) {
                
                //String used to store combined strings, make sure there is no residual 
                //values in memory from last round
                char failed_measurement_combined[10*MAX_JSON_STRING_SIZE] = {"\0, \0, \0, \0, \0, \0, \0, \0, \0, \0, "};
                int i;
              
                //Combine all continuose failed attempts
                for(i = 0; i < prevJsonStr.N_elements; i++){
                    //Concatenate all strings in array
                    strcat(failed_measurement_combined, prevJsonStr.previous_json_string[i]);
                    strcat(failed_measurement_combined, "\n");
                    //Empty the array space
                    strcpy(prevJsonStr.previous_json_string[i], "\0");
                }

                //Since strings in array is removed, set counter to zero
                prevJsonStr.N_elements = 0;
                //Send combined string to fallback server
                POSTMeasurement(FALLBACK_URL, failed_measurement_combined);
                
            }
            else if(POSTMeasurement(POST_URL ,measurement_json)){
                //Send data to temperature, if it fails, store value and send on the next 
                //two minute interval
          
                //start to fill up array with old values, increment amount of elements in array
                if(prevJsonStr.N_elements < N_OLD_JSON_STRINGS){

                    strcpy(prevJsonStr.previous_json_string[prevJsonStr.N_elements], measurement_json);
                    prevJsonStr.N_elements++;

                }
                else{
                    //When array is full, shift array to the right and add new value
                    //This way we keep the freshest values, If we want to keep the 
                    //10 first we could not send or the 10 last depends on the system
                    //This is an arbitrary choise
                    char buff[MAX_JSON_STRING_SIZE];
                    int i;
                    for(i = 1; i < N_OLD_JSON_STRINGS; i++){

                        strcpy(buff, prevJsonStr.previous_json_string[i-1]);
                        strcpy(prevJsonStr.previous_json_string[i], buff);

                    }
                    strcpy(prevJsonStr.previous_json_string[0], measurement_json);
                }
            }
        }
    }
    return 0;
}


//Takes the calculations that has been done on adc values and converts it to a raw json string
void createJSON(temperatureMeasurement * measurement, char * string, int size_of_string){
    
    char buffer[size_of_string];
    //Format data to json raw string, probably more elegant to keep json string in textfile and
    //and use a json library to parse it, but that is not a v ery embedded thing to do
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
    //In a situation I would use an interrupt vector to detect when ADC is ready
    while(1){
        //The adc_ready_flag is used to reduce time used in thread, and improve determinism of
        //sample collection time since time used to convert data does not add to the cycle time
        //with this method
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

    //Conversion from 12-bit adc to celsius when f(0)=-50 and f(4096)=50. f(x) = 0.0244x - 50 
    float temperature = 0.0244*(float)single_value_from_adc - 50.0;

    //Reset all values when data has been read, number_of_measurements are reset 
    //when post is triggered
    if(measurement->number_of_measurements == 0){
        measurement->sum = 0;
        measurement->average = 0;
        measurement->max = temperature;
        measurement->min = temperature;
        //Save dateTime from when we start to sample data
        getDateTimeISO8601(measurement->start, measurement->sizeOfStart);
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
        sendDataPOST = 0x01;
        sleep(post_adc_data_frequency);
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
        //tell curl that we want to post
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
        //URL to server and api
        curl_easy_setopt(curl, CURLOPT_URL, URL);
        //Turn on error feedback so we can catch the 500 error
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buf);

        //curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https");

        //Include json header
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        //json string to be sent over https
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, measurement_json);
        //Send data 
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