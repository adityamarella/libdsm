##Random Notes
* to remove the lock around entire locatepage_internal function, first there should be a retry incase getpage fails. Why would getpage fail? If multiple nodes call locate page at the same time, some of the nodes could receive old/stale owner information. This gets me back to the very important detail of what to do when getpage fails? Current plan is to bail out after some retries. But what happens on the node which bails out?
* Timeout because of a possible deadlock? Here is the scenario
  - Node 1 -> lock(l); getpage(p1).. blocks until Node2 responds; unlock(l) {p1 is on Node 2}
  - Node 2 -> lock(l); getpage(p2).. blocks until Node1 responds; unlock(l) {p2 is on Node 1}
  - Low recv timeout was hiding this issue all the while
  - Possible fix? Add locatepage request; never block during getpage; 
    * this changes requires every node to be connected to every other node
    * otherwise connection creation overhead will make this horribly slow
    * call locatepage before calling getpage; locatepage will always be received by the master node
    * when to update the pagetable in this case? Only soln - during locatepage
    * so the requirement is locatepage should always be followed by getpage? Is this a good design?
    * When/how to call invalidate page? Right now master is sending invalidatepage to all the nodes which are accessing the page. 
* TODO: what to do when getpage fails? 
