

# Struct ecss::IdSet

**template &lt;typename Type, bool ThreadSafe&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**IdSet**](structecss_1_1IdSet.md)



_Dense set of allocated ids, one bit per id._ [More...](#detailed-description)


































































## Detailed Description


Replaces a sorted interval list for the "which ids are live" question. An interval list is compact only while the set is contiguous: every erase in the middle of a run splits it, which is a vector insert over the interval array, and the interval count grows towards N/2 as entities die in arbitrary order. That made random-order destruction quadratic  171 ms for 200k ids, against 1.5 ms here.


A bitmap does not care about fragmentation: erase clears a bit and nothing moves.


Memory bound: take() always hands out the lowest free id and ids only ever enter through take(), so the high watermark is bounded by the _peak_ number of simultaneously live ids  it does not grow with churn. One million live ids costs 125 KB flat.




**Note:**

Intervals remain the right shape for range _filters_ (see [**Ranges**](structecss_1_1Ranges.md) and Registry::view(ranges)); this type is only for the live-id set. 





    

------------------------------
The documentation for this class was generated from the following file `ecss/IdSet.h`

