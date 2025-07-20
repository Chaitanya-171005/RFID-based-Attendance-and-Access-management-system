#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mosquitto.h>
#include <sys/stat.h>

#define MQTT_HOST "localhost"
#define MQTT_PORT 1883
#define TOPIC_IN "rfid/in"
#define TOPIC_OUT "rfid/out"
#define ACCESS_RESPONSE "access/response"
#define ACCESS_FILE "access.csv"
#define ATTENDANCE_FILE "attendance.csv"

// Store details of access granted person
struct info {
    char uid[30];
    char name[30];
    char role[30];
    char locations[128]; // Pipe-separated, e.g., "1|3"
    char valid_from[11]; // YYYY-MM-DD or ""
    char valid_until[11];
    char valid_time_start[6]; // HH:MM or ""
    char valid_time_end[6];
};

//Create a mosquitto instance
struct mosquitto *mosq;

// Function prototypes
void create_csv(const char *filename, const char *headers);
int authorize_uid(const char *uid, const char *location, struct info *employee);
void log_attendance(const char *uid, const char *name, const char *location, const char *direction);
void send_access_response(const char *uid, const char *name, int allowed);
void on_message(struct mosquitto *m, void *userdata, const struct mosquitto_message *message);
void get_info(const char *uid, struct info *employee);
int validate_entry(const char *uid, const char *new_direction, const char *new_location);
int is_time_allowed(const struct info *employee);

int main() {
    // Check if attendance and access files exist if not create the file
    create_csv(ACCESS_FILE, "UID,Name,Role,Locations,ValidFrom,ValidUntil,ValidTimeStart,ValidTimeEnd");
    create_csv(ATTENDANCE_FILE, "UID,Name,Location,Direction,Timestamp");

    // Initialize mosquitto server
    mosquitto_lib_init();
    mosq = mosquitto_new(NULL, true, NULL);
    if (!mosq) {
        fprintf(stderr, "Failed to create Mosquitto instance.\n");
        return 1;
    }
    mosquitto_message_callback_set(mosq, on_message);
    if (mosquitto_connect(mosq, MQTT_HOST, MQTT_PORT, 60)) {
        fprintf(stderr, "Unable to connect to MQTT broker.\n");
        return 1;
    }
    mosquitto_subscribe(mosq, NULL, TOPIC_IN, 0);
    mosquitto_subscribe(mosq, NULL, TOPIC_OUT, 0);
    printf("Logger running...\n");
    mosquitto_loop_forever(mosq, -1, 1);

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    return 0;
}

void create_csv(const char *filename, const char *headers) {
    struct stat st;
    if (stat(filename, &st) != 0) { // stat function checks for the existence of the file returns 0 if file exists
        FILE *file = fopen(filename, "w");
        if (file) {
            fprintf(file, "%s\n", headers);
            fclose(file);
            printf("Created %s with headers.\n", filename);
        } else {
            perror("Failed to create file");
        }
    }
}

int authorize_uid(const char *uid, const char *location, struct info *employee) {
    FILE *file = fopen(ACCESS_FILE, "r");
    if (!file) {
        perror("Cannot open access file");
        return 0;
    }
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = 0;
        char *token = strtok(line, ",");
        if (token && strcmp(token, uid) == 0) {
            strcpy(employee->uid, token);
            token = strtok(NULL, ","); // Name
            strcpy(employee->name, token ? token : "");
            token = strtok(NULL, ","); // Role
            strcpy(employee->role, token ? token : "");
            token = strtok(NULL, ","); // Locations
            strcpy(employee->locations, token ? token : "");
            token = strtok(NULL, ","); // ValidFrom
            strcpy(employee->valid_from, token ? token : "");
            token = strtok(NULL, ","); // ValidUntil
            strcpy(employee->valid_until, token ? token : "");
            token = strtok(NULL, ","); // ValidTimeStart
            strcpy(employee->valid_time_start, token ? token : "");
            token = strtok(NULL, ","); // ValidTimeEnd
            strcpy(employee->valid_time_end, token ? token : "");

            // Check location
            char *loc = strtok(employee->locations, "|");
            int location_allowed = 0;
            while (loc) {
                if (strcmp(loc, location) == 0) {
                    location_allowed = 1;
                    break;
                }
                loc = strtok(NULL, "|");
            }
            
            if (!location_allowed) {
                fclose(file);
                return 0;
            }

            // Check time restrictions
            if (!is_time_allowed(employee)) {
                fclose(file);
                return 0;
            }

            fclose(file);
            return 1;
        }
    }
    fclose(file);
    return 0;
}

void get_info(const char *uid, struct info *employee) {
    FILE *file = fopen(ACCESS_FILE, "r");
    if (!file) {
        perror("Cannot open access file");
        strcpy(employee->uid, uid);
        strcpy(employee->name, "Unknown");
        strcpy(employee->role, "Unknown");
        strcpy(employee->locations, "");
        strcpy(employee->valid_from, "");
        strcpy(employee->valid_until, "");
        strcpy(employee->valid_time_start, "");
        strcpy(employee->valid_time_end, "");
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = 0;
        char *token = strtok(line, ",");
        if (token && strcmp(token, uid) == 0) {
            strcpy(employee->uid, token);
            token = strtok(NULL, ","); // Name
            strcpy(employee->name, token ? token : "");
            token = strtok(NULL, ","); // Role
            strcpy(employee->role, token ? token : "");
            token = strtok(NULL, ","); // Locations
            strcpy(employee->locations, token ? token : "");
            token = strtok(NULL, ","); // ValidFrom
            strcpy(employee->valid_from, token ? token : "");
            token = strtok(NULL, ","); // ValidUntil
            strcpy(employee->valid_until, token ? token : "");
            token = strtok(NULL, ","); // ValidTimeStart
            strcpy(employee->valid_time_start, token ? token : "");
            token = strtok(NULL, ","); // ValidTimeEnd
            strcpy(employee->valid_time_end, token ? token : "");
            fclose(file);
            return;
        }
    }
}

void log_attendance(const char *uid, const char *name, const char *location, const char *direction) {
    FILE *file = fopen(ATTENDANCE_FILE, "a");
    if (!file) {
        perror("Failed to open attendance log");
        return;
    }
    time_t now = time(NULL);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(file, "%s,%s,%s,%s,%s\n", uid, name, location, direction, timeStr);
    fclose(file);
    printf("Logged: %s,%s,%s,%s,%s\n", uid, name, location, direction, timeStr);
}

void send_access_response(const char *uid, const char *name, int allowed) {
    // 1 - Allowed, 0-> Access Denied
    char response[128];
    char access[8];
    strcpy(access, allowed ? "ALLOWED" : "DENIED");
    snprintf(response, sizeof(response), "%s,%s,%s", uid, access, name);
    mosquitto_publish(mosq, NULL, ACCESS_RESPONSE, strlen(response), response, 0, false);
}

int is_time_allowed(const struct info *employee) {
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    char current_date[11];
    char current_time[6];
    strftime(current_date, sizeof(current_date), "%Y-%m-%d", tm_now);
    strftime(current_time, sizeof(current_time), "%H:%M", tm_now);

    // Check date range
    if (employee->valid_from[0] != '\0' && employee->valid_until[0] != '\0') {
        if (strcmp(current_date, employee->valid_from) < 0 || strcmp(current_date, employee->valid_until) > 0) {
            printf("Access denied for %s: Outside valid date range %s to %s\n",
                   employee->uid, employee->valid_from, employee->valid_until);
            return 0;
        }
    }

    // Check time range
    if (employee->valid_time_start[0] != '\0' && employee->valid_time_end[0] != '\0') {
        if (strcmp(current_time, employee->valid_time_start) < 0 || strcmp(current_time, employee->valid_time_end) > 0) {
            printf("Access denied for %s: Outside valid time range %s to %s\n",
                   employee->uid, employee->valid_time_start, employee->valid_time_end);
            return 0;
        }
    }

    return 1;
}

int validate_entry(const char *uid, const char *new_direction, const char *new_location) {
    FILE *file = fopen(ATTENDANCE_FILE, "r");
    if (!file) {
        perror("Cannot open attendance file");
        return 1; // No file, treat as first entry
    }

    char line[256];
    char last_direction[4] = "";
    char last_location[10] = "";
    char last_uid[30];
    int is_inside = 0;

    // Skip header
    fgets(line, sizeof(line), file);

    // Read file to find last entry for UID
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = 0;
        char *token = strtok(line, ",");
        if (token && strcmp(token, uid) == 0) {
            strcpy(last_uid, token);
            token = strtok(NULL, ","); // Name
            token = strtok(NULL, ","); // Location
            if (token) strcpy(last_location, token);
            token = strtok(NULL, ","); // Direction
            if (token) strcpy(last_direction, token);
            // Update inside status
            is_inside = (strcmp(last_direction, "IN") == 0) ? 1 : 0;
        }
    }
    fclose(file);

    // Validate sequence and location
    if (strcmp(new_direction, "IN") == 0) {
        if (!is_inside) {
            // User is not inside any location, IN is valid
            return 1;
        }
        printf("Invalid sequence for UID %s: Already IN at %s, attempted IN at %s\n", uid, last_location, new_location);
        return 0;
    } else if (strcmp(new_direction, "OUT") == 0) {
        if (is_inside && strcmp(last_location, new_location) == 0) {
            // User is inside this location, OUT is valid
            return 1;
        }
        if (!is_inside) {
            printf("Invalid sequence for UID %s: No prior IN, attempted OUT at %s\n", uid, new_location);
        } else {
            printf("Invalid location for UID %s: IN at %s, attempted OUT at %s\n", uid, last_location, new_location);
        }
        return 0;
    }
    printf("Invalid direction for UID %s: %s\n", uid, new_direction);
    return 0;
}

void on_message(struct mosquitto *m, void *userdata, const struct mosquitto_message *message) {
    char *topic = message->topic;
    char *payload = (char *)message->payload;

    // Parse payload (e.g., "5A4942,1")
    char uid[30];
    char location[10];
    snprintf(uid, sizeof(uid), "%s", payload);// Copies payload msg recieved to uid
    // Using strchr to find comma and separate uid and location oand store them in location and uid variables
    char *comma = strchr(uid, ',');
    if (comma) {
        *comma = '\0'; // Split string  and store location
        strcpy(location, comma + 1);  
    } else {
        strcpy(location, "Unknown"); // If comma not found, location is "Unknown"
    }

    // Validate location ID (1-5)
    int loc_id = atoi(location);
    if (loc_id < 1 || loc_id > 5) {
        printf("Invalid location ID: %s\n", location);
        send_access_response(uid, "Unknown", 0);
        return;
    }

    // Get user info
    struct info employee;
    get_info(uid, &employee);

    // Determine direction
    const char *direction = (strcmp(topic, TOPIC_IN) == 0) ? "IN" : "OUT";

    // Check all conditions: location, time, and sequence
    int is_authorised = authorize_uid(uid, location, &employee) && validate_entry(uid, direction, location);

    if (is_authorised) {
        log_attendance(uid, employee.name, location, direction);
    }
    send_access_response(uid, employee.name, is_authorised);
    strcpy(employee.name,"UNKNOWN");
}
