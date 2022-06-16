# Temperature Sensor

Compile with Os flag instead of g to go from 40k to 20k executable size

You're free to solve the task using whatever programming language you prefer, but I recommend that you choose a language you are comfortable with.
Functional code should be delivered in a git repository with working commits.

The specification further down in this document defines a minimum set of requirements for the application.

## Background
The function (in C-like code):
```
double getTemperature();
```
Returns a temperature reading from a temperature sensor placed in a fairly chaotic environment.
You can asssume that the value has been read using an ADC with 12-bit resolution, with a reference voltage of 3.3V.
The temperature sensor reports a temperature range of -50C to +50C and can be read every 100ms.
For example the ADC can read the following values from the sensor:
- 2048 (rougly 0C)
- 3000 (rougly 23C)

For the sake of this exercise, the ADC values will be provided in a text file `temperature.txt` with a single value on every line.

## Task
Write an application that reads ADC output from the text file, converts it to temperature in celsius, and sends it to a HTTP REST endpoint. With the following requirements:

- The `getTemperature` function should be expanded to read a single value from the text file `temperature.txt`.
A new value can only be read every 100ms.
- The application should further calculate the max, min, and average temperature over a period of 2 minutes.
- Every 2 minutes the application should send the stored values to a HTTP REST API with the address `http://localhost:5000/api/temperature` using a HTTP POST request.
- The values should be sent in JSON format using the following structure denoted by TemperatureMeasurement:
```
// TemperatureMeasurement
{
	"time": {
		"start": string, // Start date and time in ISO8601 format for the measurement
		"end": string // End date and time in ISO8601 format for the measurement
	},
	"min": number, // Minimum observed temperature
	"max": number, // Maximum observed temperature
	"average": number // Average temperature
}
```

The values denote the field types and are not example values. All time values should follow the ISO 8601 standard with both date and time in UTC. All number values should be float/double values with a maximum of two digits after the decimal.

## Error handling
It must be expected that the backend will fail quite often and return the HTTP status code 500.
All errors that could happen on the server side should be handled by temporarily storing the last produced TemperatureMeasurement.
If a failure has occured the previously failed TemperatureMeasurement values should be sent on the next 2 minute interval.
Upon failure, the last maximum 10 TemperatureMeasurement values should be sent to an alternative endpoint `http://localhost:5000/api/temperature/missing` in JSON array format, e.g.:
```
[
	{
		"time": {
			"start": string,
			"end": string
		},
		"min": number,
		"max": number,
		"average": number
	},
	{
		"time": {
			"start": string,
			"end": string
		},
		"min": number,
		"max": number,
		"average": number
	},
	...
]
```

## Server side software
A piece of server side software is provided that is implemented using ASP.NET that can be used for testing.
The software exposes an API on the address `http://localhost:5000`.
The endpoints mentioned further up in this doc receives temperature values, performs simple validation of the input, formats it, and stores it in a text file `output.txt` relative to the executable.
Example `output.txt`:
```
[2021-04-12T20:20:20.0000000Z-2021-04-12T20:22:20.0000000Z] max: 1.13 min: 1.11 avg: 1.12
[2021-04-12T20:22:20.0000000Z-2021-04-12T20:24:20.0000000Z] max: 1.15 min: 1.11 avg: 1.13
[2021-04-12T20:24:20.0000000Z-2021-04-12T20:26:20.0000000Z] max: 1.14 min: 1.12 avg: 1.13
```

The endpoints are setup to "randomly" fail and return status code 500.

There are portable 64-bit binaries available in the Releases tab for Linux, Windows, and MacOS.
- For Windows: Extract the zip and run `backend.exe`
- For Linux and Mac: Extract the zip and run `backend`
