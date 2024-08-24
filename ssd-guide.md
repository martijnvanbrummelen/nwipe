# Guide for the cancellation of solid state media (Sata / SAS / NVME)

# Index
1. Disclaimer & Warning
2. Current Standard Commands for Sanitization
3. Manufacturer and Vendor Support for Sanitization
4. Advised Procedure for Sanitization of Drives

# Disclaimer & Warning
The following information is given without any warranty and indicates best practices as of the writing of this content.
All information should be validated for the precise manufacturer / vendor SKU you want to sanitize.
Any sanitization process should be validated by comparing the information contained on the disk before and after sanitization assuring that the previously stored data has been destroyed.
Given that most of the manufacturer tools available today are closed source, it is not possible to determine with security if the respective tool does or does not effectively verify the outcome of a sanitization.
To assure a successful sanitization it is highly recommended to compare the data contained on the disk before and after sanitization and use the manufacturer tool together with tools such as nwipe. 

## Current Standard Commands for Sanitization

All major block device interface standards contain optional block commands for sanitization. 
Below are the standards listed for reference: 
* [SATA ATA/ATAPI Sanitization Command according to ACS-3 Standard](https://people.freebsd.org/~imp/asiabsdcon2015/works/d2161r5-ATAATAPI_Command_Set_-_3.pdf);
* [SAS Sanitization Command according to SBC-4 Standard](https://www.t10.org/members/w_sbc4.htm);
* [NVME Sanitization Command according to NVME Command Set Specification](https://nvmexpress.org/wp-content/uploads/NVM-Express-NVM-Command-Set-Specification-Revision-1.1-2024.08.05-Ratified.pdf).

## Manufacturer and Vendor Support for Sanitization
Given that the support for sanitization is optional and not a mandatory aspect of the respective standards the support accross vendors can vary largely.
The following table does not claim or warrant to be complete, it is highly advised to always validate the information with the manufacturer:

| Manufacturer    	| Manufacturer Tool (MFT)                              	| SATA            	| SAS               	| NVME     	|
|-----------------	|------------------------------------------------------	|-----------------	|-------------------	|----------	|
| Samsung         	| Samsung DC Toolkit 2.1                               	| Use MFT*        	| Use MFT           	| Use MFT  	|
| Intel / Solidigm  | Solidigm™ Storage Tool                               	| Use MFT*\*        	| Use MFT           	| Use MFT  	|
| Western Digital 	| supports SAS / SCSI format unit command        	    | hdparm sanitize 	| sg_utils sanitize 	| nvme-cli sanitize	|
| Sandisk         	| supports SAS / SATA / SCSI format unit command 	    | hdparm sanitize 	| sg_utils sanitize 	| nvme-cli sanitize	|
| Seagate         	| Open Seachest 	    | Use MFT 	| Use MFT 	| Use MFT	|
| SK Hynix        	| Unconfirmed for Linux                                	| N/A             	| N/A               	| N/A      	|
| Kioxia          	| Unconfirmed for Linux                                	| N/A             	| N/A               	| N/A      	|
| Micron          	| Unconfirmed for Linux                                	| N/A             	| N/A               	| N/A      	|
| Kingston        	| Unconfirmed for Linux                                	| N/A             	| N/A               	| N/A      	|
| Others          	| Unconfirmed for Linux                                	| N/A             	| N/A               	| N/A      	|

\*      [DC Toolkit 3.0 contains hdparm, but Interface Standard Compliance is unconfirmed](https://download.semiconductor.samsung.com/resources/user-manual/Samsung_DCToolkit_V3.0_User_Guide.pdf)
\*\*    [Generally Supported, but individual models might offer different levels of support](https://community.solidigm.com/t5/solid-state-drives-nand/support-for-sata-sanitize-command/td-p/24452)

## Advised Procedure for Sanitization of SSD Drives

1.  Complete an intial sanitization using the manufacturer tools or if supported by the manufacturer use hdparm, sg_utils or nvme;
2.  Follow up with SHREDOS/Nwipe with a single PRNG stream with verification (PRNG data is extremely hard if not impossible to compress and therefor has to be written out by the firmware);
3.  Complete an additional sanitization using the manufacturer tools or if supported by the manufacturer use hdparm, sg_utils or nvme;
4.  Validate that the data has been overwritten.