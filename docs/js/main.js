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
    simpleSearch.addEventListener("submit", advancedsearch)

    docLoc = document.location;
    if (docLoc.protocol != "file:" ||
        docLoc.origin != "null" ||
        docLoc.host !== "" ||
        docLoc.hostname !== "") {
        fetchRSS()
    }
}

function fetchRSS() {
    cb = "jsonpRSSFeedCallback"
    window["jsonpRSSFeedCallback"] = function (data) {
        if (data.responseStatus != 200)
            return
        entries = data.responseData.feed.entries
        nEntries = Math.min(entries.length, 4)

        dl = document.createElement('dl')

        dateOpts = { day: 'numeric', month: 'short', year: 'numeric'}

        for (i = 0; i < nEntries; i++) {
            entry = entries[i]
            a = document.createElement('a')
            a.href = entry.link
            a.innerText = entry.title

            dt = document.createElement('dt')
            dt.appendChild(a)
            dl.appendChild(dt)

            date = new Date(entry.publishedDate)
            datestr = date.toLocaleDateString('default', dateOpts)

            dd = document.createElement('dd')
            dd.innerText = ` by ${entry.author} on ${datestr}`

            dl.appendChild(dd)
        }

        planet.appendChild(dl);
    };
    script = document.createElement("script")
    script.src = "https://feedrapp.herokuapp.com/"
    script.src += `?q=http%3A%2F%2Fplanet.virt-tools.org%2Fatom.xml&callback=${cb}`
    document.body.appendChild(script);
}

function advancedsearch(e) {
    e.preventDefault();
    e.stopPropagation();

    form = document.createElement("form");
    form.setAttribute("method", "get");

    newq = document.createElement("input");
    newq.setAttribute("type", "hidden");
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
        form.setAttribute("action", "https://google.com/search");
        newq.setAttribute("name", "q");
        newq.value = "site:libvirt.org " + q.value;
    } else if (what == "wiki") {
        form.setAttribute("action", "https://wiki.libvirt.org/index.php");
        newq.setAttribute("name", "search");
        newq.value = q.value;
    } else if (what == "devs") {
        form.setAttribute("action", "https://google.com/search");
        newq.setAttribute("name", "q");
        newq.value = "site:redhat.com/archives/libvir-list " + q.value;
    } else if (what == "users") {
        form.setAttribute("action", "https://google.com/search");
        newq.setAttribute("name", "q");
        newq.value = "site:redhat.com/archives/libvirt-users " + q.value;
    }

    document.body.appendChild(form);
    form.submit();

    return false;
}
