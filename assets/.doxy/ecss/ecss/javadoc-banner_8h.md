

# File javadoc-banner.h



[**FileList**](files.md) **>** [**doxygen-1.14.0**](dir_9d5bad020669189c90cda983471be5d0.md) **>** [**examples**](dir_8400fc686cf1eec637c6139505ac43d7.md) **>** [**javadoc-banner.h**](javadoc-banner_8h.md)

[Go to the source code of this file](javadoc-banner_8h_source.md)








































## Public Functions

| Type | Name |
| ---: | :--- |
|  void | [**cstyle**](#function-cstyle) (int theory) <br> |
|  void | [**doxygenBanner**](#function-doxygenbanner) (int theory) <br> |
|  void | [**javadocBanner**](#function-javadocbanner) (int theory) <br> |




























## Public Functions Documentation




### function cstyle 

```C++
void cstyle (
    int theory
) 
```



A brief history of JavaDoc-style (C-style) comments.


This is the typical JavaDoc-style C-style comment. It starts with two asterisks.




**Parameters:**


* `theory` Even if there is only one possible unified theory. it is just a set of rules and equations. 




        

<hr>



### function doxygenBanner 

```C++
void doxygenBanner (
    int theory
) 
```



A brief history of Doxygen-style banner comments.


This is a Doxygen-style C-style "banner" comment. It starts with a "normal" comment and is then converted to a "special" comment block near the end of the first line. It is written this way to be more "visible" to developers who are reading the source code. This style of commenting behaves poorly with clang-format.




**Parameters:**


* `theory` Even if there is only one possible unified theory. it is just a set of rules and equations. 




        

<hr>



### function javadocBanner 

```C++
void javadocBanner (
    int theory
) 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `doxygen-1.14.0/examples/javadoc-banner.h`

