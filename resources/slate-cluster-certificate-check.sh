#!/bin/bash

# Help message
if [ "$#" -ne 3 ]; then
    echo "Checks whether the certificate of a cluster is about to expire and sends output to a provided email"
    echo
    echo "      $0 KUBECONF CLUSTERNAME EMAIL"
    echo
    echo "Example:  $0 ~/.kube/conf mycluster my@email.com"
    exit -1
fi


kubectlconfig=$1
clustername=$2
email=$3

# TODO:
# Include a tighter warning bound (less than 15 days) x
# Include doc resource link in email x
# Test script
# Determine SLATE server runtime location (to see how the script can be run by the server)
# If that can be determined, then test the server's running of the script.

IP=$(cat $kubectlconfig 2>/dev/null | grep server | awk '{print $2}' | cut -d'/' -f3)
ENDDATE=$(timeout 5 openssl s_client -showcerts -connect "$IP" 2>/dev/null | openssl x509 -noout -enddate 2>/dev/null | cut -d'=' -f2)
if [ -n "$ENDDATE" ]; then
  if [ $(date -d "$ENDDATE" +%s) -gt $(date -d "now + 30 days" +%s) ]; then
    echo 0 SLATE-cluster-$clustername-certificate - Certificate is valid and will expire in more than 30 days: $IP \($ENDDATE\)
  else
    if [ $(date -d "$ENDDATE" +%s) -gt $(date -d "now + 15 days" +%s) ]; then
      echo SLATE-cluster-$clustername-certificate - Certificate is valid but will expire in less than 30 days: $IP \($ENDDATE\). For more help see https://slateci.io/docs/resources/k8s-certificates.html | mail -s "SLATE: Cluster Certificate Expiration Impending" $email
    else
      if [ $(date -d "$ENDDATE" +%s) -gt $(date -d "now + 1 days" +%s) ]; then
        echo SLATE-cluster-$clustername-certificate - Certificate is valid but will expire in two weeks or less: $IP \($ENDDATE\). For more help see https://slateci.io/docs/resources/k8s-certificates.html | mail -s "SLATE: Cluster Certificate Expiration Impending" $email
      else
        echo SLATE-cluster-$clustername-certificate - Certificate is invalid or needs to be renewed: $IP \($ENDDATE\). For more help see https://slateci.io/docs/resources/k8s-certificates.html | mail -s "SLATE: Cluster Certificate Expiration" $email
      fi
    fi
  fi
else
  echo 1 SLATE-cluster-$clustername-certificate - Endpoint/certificate was not found: IP \($ENDDATE\)
fi
