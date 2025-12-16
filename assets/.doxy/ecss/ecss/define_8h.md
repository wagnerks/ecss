

# File define.h



[**FileList**](files.md) **>** [**doxygen-1.14.0**](dir_9d5bad020669189c90cda983471be5d0.md) **>** [**examples**](dir_8400fc686cf1eec637c6139505ac43d7.md) **>** [**define.h**](define_8h.md)

[Go to the source code of this file](define_8h_source.md)

_testing defines_ [More...](#detailed-description)

































































## Macros

| Type | Name |
| ---: | :--- |
| define  | [**ABS**](define_8h.md#define-abs) (x) `(((x)&gt;0)?(x):-(x))`<br>_Computes the absolute value of its argument_ _x_ _._ |
| define  | [**MAX**](define_8h.md#define-max) (x, y) `((x)&gt;(y)?(x):(y))`<br> |
| define  | [**MIN**](define_8h.md#define-min) (x, y) `((x)&gt;(y)?(y):(x))`<br> |

## Detailed Description


This is to test the documentation of defines. 


    
## Macro Definition Documentation





### define ABS 

_Computes the absolute value of its argument_ _x_ _._
```C++
#define ABS (
    x
) `(((x)>0)?(x):-(x))`
```





**Parameters:**


* `x` input value. 



**Returns:**

absolute value of _x_. 





        

<hr>



### define MAX 

```C++
#define MAX (
    x,
    y
) `((x)>(y)?(x):(y))`
```



Computes the maximum of _x_ and _y_. 


        

<hr>



### define MIN 

```C++
#define MIN (
    x,
    y
) `((x)>(y)?(y):(x))`
```



Computes the minimum of _x_ and _y_. 


        

<hr>

------------------------------
The documentation for this class was generated from the following file `doxygen-1.14.0/examples/define.h`

