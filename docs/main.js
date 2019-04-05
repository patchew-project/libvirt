function pageload() {
    window.addEventListener('scroll', function(e){
        var distanceY = window.pageYOffset || document.documentElement.scrollTop
        var shrinkOn = 94
        home = document.getElementById("home");
        links = document.getElementById("jumplinks");
        search = document.getElementById("search");
        body = document.getElementById("body");
        if (distanceY > shrinkOn) {
            if (home.className != "navhide") {
                body.className = "navhide"
                home.className = "navhide"
                links.className = "navhide"
                search.className = "navhide"
            }
        } else {
            if (home.className == "navhide") {
                body.className = ""
                home.className = ""
                links.className = ""
                search.className = ""
            }
        }
    });

    /* Setting this class makes the advanced search options visible */
    advancedSearch = document.getElementById("advancedsearch")
    advancedSearch.className = "advancedsearch"

    simpleSearch = document.getElementById("simplesearch")
    simplesearch.addEventListener("submit", advancedsearch)
}

function advancedsearch(e) {
    e.preventDefault();
    e.stopPropagation();

    form = document.createElement("form");
    form.setAttribute("method", "get");
    form.setAttribute("action", "https://google.com/search");

    newq = document.createElement("input");
    newq.setAttribute("type", "hidden");
    newq.setAttribute("name", "q");
    form.appendChild(newq);

    q = document.getElementById("searchq");
    whats = document.getElementsByName("what");
    what = "website";
    for (var i = 0; i < whats.length; i++) {
	if (whats[i].checked) {
	    what = whats[i].value;
	    break;
	}
    }

    if (what == "website") {
	newq.value = "site:libvirt.org " + q.value;
    } else if (what == "wiki") {
	newq.value = "site:wiki.libvirt.org " + q.value;
    } else if (what == "lists") {
	newq.value = "site:redhat.com inurl:/archives/libvir " + q.value;
    }

    document.body.appendChild(form);
    form.submit();

    return false;
}
