

# Struct ecss::detail::AccessTracker::State



[**ClassList**](annotated.md) **>** [**State**](structecss_1_1detail_1_1AccessTracker_1_1State.md)


























## Public Attributes

| Type | Name |
| ---: | :--- |
|  std::mutex | [**mutex**](#variable-mutex)  <br> |
|  std::map&lt; std::thread::id, int &gt; | [**readers**](#variable-readers)  <br>_reentrant depth per thread_  |
|  std::thread::id | [**writer**](#variable-writer)   = `{}`<br> |
|  int | [**writerDepth**](#variable-writerdepth)   = `0`<br> |












































## Public Attributes Documentation




### variable mutex 

```C++
std::mutex ecss::detail::AccessTracker::State::mutex;
```




<hr>



### variable readers 

_reentrant depth per thread_ 
```C++
std::map<std::thread::id, int> ecss::detail::AccessTracker::State::readers;
```




<hr>



### variable writer 

```C++
std::thread::id ecss::detail::AccessTracker::State::writer;
```




<hr>



### variable writerDepth 

```C++
int ecss::detail::AccessTracker::State::writerDepth;
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/AccessTracker.h`

