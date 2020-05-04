==============================
Libvirt Continuous Integration
==============================

The libvirt project primarily uses GitLab CI for automated testing of Linux
builds, and cross-compiled Windows builds. `Travis <https://travis-ci.org/libvirt/libvirt>`_
is used for validating macOS builds, and `Jenkins <https://ci.centos.org/view/libvirt>`_
is temporarily used for validating FreeBSD builds.

GitLab CI Dashboard
===================

The dashboard below shows the current status of the GitLab CI jobs for each
repository:

.. raw:: html

   <table>
     <thead>
       <tr>
         <th>Project</th>
         <th>Pipeline</th>
       </tr>
     </thead>
     <tbody>
       <tr>
         <th colspan="2">Core</th>
       </tr>
       <tr>
         <td>libvirt</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <th colspan="2">Language bindings</th>
       </tr>
       <tr>
         <td>libvirt-csharp</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-csharp/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-csharp/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-go</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-go/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-go/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-java</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-java/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-java/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-ocaml</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-ocaml/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-ocaml/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-perl</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-perl/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-perl/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-php</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-php/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-php/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-python</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-python/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-python/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-rust</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-rust/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-rust/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>ruby-libvirt</td>
         <td>
           <a href="https://gitlab.com/libvirt/ruby-libvirt/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/ruby-libvirt/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <th colspan="2">Object mappings</th>
       </tr>
       <tr>
         <td>libvirt-cim</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-cim/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-cim/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-dbus</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-dbus/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-dbus/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-glib</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-glib/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-glib/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-go-xml</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-go-xml/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-go-xml/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-snmp</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-snmp/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-snmp/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <th colspan="2">Testing</th>
       </tr>
       <tr>
         <td>libvirt-ci</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-ci/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-ci/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-dockerfiles</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-dockerfiles/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-dockerfiles/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-test-API</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-test-API/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-test-API/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-tck</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-tck/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-tck/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <th colspan="2">Docs / web</th>
       </tr>
       <tr>
         <td>libvirt-publican</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-publican/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-publican/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-appdev-guide-python</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-appdev-guide-python/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-appdev-guide-python/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-wiki</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-wiki/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-wiki/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>virttools-planet</td>
         <td>
           <a href="https://gitlab.com/libvirt/virttools-planet/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/virttools-planet/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>virttools-web</td>
         <td>
           <a href="https://gitlab.com/libvirt/virttools-web/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/virttools-web/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <th colspan="2">Misc</th>
       </tr>
       <tr>
         <td>libvirt-console-proxy</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-console-proxy/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-console-proxy/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-designer</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-designer/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-designer/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-devaddr</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-devaddr/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-devaddr/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-sandbox</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-sandbox/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-sandbox/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-sandbox-image</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-sandbox-image/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-sandbox-image/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-security-notice</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-security-notice/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-security-notice/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>test</td>
         <td>
           <a href="https://gitlab.com/libvirt/test/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/test/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <th colspan="2">Archived</th>
       </tr>
       <tr>
         <td>autotest</td>
         <td>
           <a href="https://gitlab.com/libvirt/autotest/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/autotest/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>cimtest</td>
         <td>
           <a href="https://gitlab.com/libvirt/cimtest/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/cimtest/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>hooks</td>
         <td>
           <a href="https://gitlab.com/libvirt/hooks/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/hooks/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libcmpiutil</td>
         <td>
           <a href="https://gitlab.com/libvirt/libcmpiutil/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libcmpiutil/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-appdev-guide</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-appdev-guide/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-appdev-guide/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-autobuild</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-autobuild/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-autobuild/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-builder</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-builder/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-builder/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-kube</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-kube/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-kube/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-media</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-media/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-media/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-qpid</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-qpid/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-qpid/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>libvirt-virshcmdref</td>
         <td>
           <a href="https://gitlab.com/libvirt/libvirt-virshcmdref/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/libvirt-virshcmdref/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>patchchecker</td>
         <td>
           <a href="https://gitlab.com/libvirt/patchchecker/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/patchchecker/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>php-virt-control</td>
         <td>
           <a href="https://gitlab.com/libvirt/php-virt-control/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/php-virt-control/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
       <tr>
         <td>scripts</td>
         <td>
           <a href="https://gitlab.com/libvirt/scripts/pipelines">
             <img alt="pipeline status" src="https://gitlab.com/libvirt/scripts/badges/master/pipeline.svg"/>
           </a>
         </td>
       </tr>
     </tbody>
   </table>
