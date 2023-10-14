#ifndef CONF_H_
#define CONF_H_

/**
 * Initialises the libconfig code, called once at the
 * start of nwipe, prior to any attempts to access
 * nwipe's config file /etc/nwipe/nwipe.conf
 * @param none
 * @return int
 *   0  = success
 *   -1 = error
 */
int nwipe_conf_init();

/**
 * Before exiting nwipe, this function should be called
 * to free up libconfig's memory usage
 * @param none
 * @return void
 */
void nwipe_conf_close();

void save_selected_customer( char** );

/**
 * int nwipe_conf_update_setting( char *, char * );
 * Use this function to update a setting in nwipe.conf
 * @param char * this is the group name and setting name separated by a period '.'
 *               i.e "PDF_Certificate.PDF_Enable"
 * @param char * this is the setting, i.e ENABLED
 * @return int 0 = Success
 *             1 = Unable to update memory copy
 *             2 = Unable to write new configuration to /etc/nwipe/nwipe.conf
 */
int nwipe_conf_update_setting( char*, char* );

/**
 * int nwipe_conf_read_setting( char *, char *, const char ** )
 * Use this function to read a setting value in nwipe.conf
 * @param char * this is the group name
 * @param char * this is the setting name
 * @param char ** this is a pointer to the setting value
 * @return int 0 = Success
 *             -1 = Unable to find the specified group name
 *             -2 = Unable to find the specified setting name
 */
int nwipe_conf_read_setting( char*, const char** );

#define FIELD_LENGTH 256
#define NUMBER_OF_FIELDS 4

#endif /* CONF_H_ */
