

# File structcmd.h



[**FileList**](files.md) **>** [**doxygen-1.14.0**](dir_9d5bad020669189c90cda983471be5d0.md) **>** [**examples**](dir_8400fc686cf1eec637c6139505ac43d7.md) **>** [**structcmd.h**](structcmd_8h.md)

[Go to the source code of this file](structcmd_8h_source.md)

_A Documented file._ [More...](#detailed-description)


















## Public Types

| Type | Name |
| ---: | :--- |
| typedef unsigned int | [**UINT32**](#typedef-uint32)  <br>_A type definition for a ._  |




## Public Attributes

| Type | Name |
| ---: | :--- |
|  int | [**errno**](#variable-errno)  <br>_Contains the last error code._  |
















## Public Functions

| Type | Name |
| ---: | :--- |
|  int | [**close**](#function-close) (int fd) <br>_Closes the file descriptor_ _fd_ _._ |
|  int | [**open**](#function-open) (const char \* pathname, int flags) <br>_Opens a file descriptor._  |
|  int | [**read**](#function-read) (int fd, char \* buf, size\_t count) <br>_Read bytes from a file descriptor._  |
|  size\_t | [**write**](#function-write) (int fd, const char \* buf, size\_t count) <br>_Writes_ _count_ _bytes from__buf_ _to the filedescriptor__fd_ _._ |



























## Macros

| Type | Name |
| ---: | :--- |
| define  | [**MAX**](structcmd_8h.md#define-max) (a, b) `(((a)&gt;(b))?(a):(b))`<br>_A macro that returns the maximum of_ _a_ _and__b_ _._ |

## Detailed Description


Details. 


    
## Public Types Documentation




### typedef UINT32 

_A type definition for a ._ 
```C++
typedef unsigned int UINT32;
```



Details. 


        

<hr>
## Public Attributes Documentation




### variable errno 

_Contains the last error code._ 
```C++
int errno;
```





**Warning:**

Not thread safe! 





        

<hr>
## Public Functions Documentation




### function close 

_Closes the file descriptor_ _fd_ _._
```C++
int close (
    int fd
) 
```





**Parameters:**


* `fd` The descriptor to close. 




        

<hr>



### function open 

_Opens a file descriptor._ 
```C++
int open (
    const char * pathname,
    int flags
) 
```





**Parameters:**


* `pathname` The name of the descriptor. 
* `flags` Opening flags. 




        

<hr>



### function read 

_Read bytes from a file descriptor._ 
```C++
int read (
    int fd,
    char * buf,
    size_t count
) 
```





**Parameters:**


* `fd` The descriptor to read from. 
* `buf` The buffer to read into. 
* `count` The number of bytes to read. 




        

<hr>



### function write 

_Writes_ _count_ _bytes from__buf_ _to the filedescriptor__fd_ _._
```C++
size_t write (
    int fd,
    const char * buf,
    size_t count
) 
```





**Parameters:**


* `fd` The descriptor to write to. 
* `buf` The data buffer to write. 
* `count` The number of bytes to write. 




        

<hr>
## Macro Definition Documentation





### define MAX 

_A macro that returns the maximum of_ _a_ _and__b_ _._
```C++
#define MAX (
    a,
    b
) `(((a)>(b))?(a):(b))`
```



Details. 


        

<hr>

------------------------------
The documentation for this class was generated from the following file `doxygen-1.14.0/examples/structcmd.h`

