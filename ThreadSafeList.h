#ifndef THREAD_SAFE_LIST_H_
#define THREAD_SAFE_LIST_H_

#include <pthread.h>
#include <iostream>
#include <iomanip> // std::setw

using namespace std;

template <typename T>
class List 
{
    public:
        class Node;
        /**
         * Constructor
         */
        List() :  head(nullptr), dummy(Node()){
            counter = 0;

            dummy.next = nullptr;
            pthread_mutex_init(&head_m, nullptr);
            pthread_mutex_init(&counter_m, nullptr);
        }

        /**
         * Destructor
         */
        ~List() {
            pthread_mutex_lock(&dummy.m);
            Node* node = dummy.next;
            while(node != nullptr){
                pthread_mutex_lock(&node->m);
                pthread_mutex_unlock(&node->m);
                Node* tmp = node;
                node = node->next;
                delete tmp;
            }
            pthread_mutex_destroy(&counter_m);

            pthread_mutex_destroy(&head_m);

            pthread_mutex_unlock(&dummy.m);

        }

        class Node {
         public:
          T data;
          Node *next;
          pthread_mutex_t m{};
          Node(){pthread_mutex_init(&m, nullptr);};
          explicit Node(const T& data_in, Node* next_in = nullptr) : data(data_in), next(next_in), m(PTHREAD_MUTEX_INITIALIZER) {}
          ~Node() {
              pthread_mutex_destroy(&m);
          }
        };

        /**
         * Insert new node to list while keeping the list ordered in an ascending order
         * If there is already a node has the same data as @param data then return false (without adding it again)
         * @param data the new data to be added to the list
         * @return true if a new node was added and false otherwise
         */
        bool insert(const T& data) {
            Node *pred, *curr;
            pred = &dummy;
            pthread_mutex_lock(&pred->m);
            if(pred->next == nullptr || pred->next->data > data){
                Node* node = new Node(data);
                if(pred->next != nullptr){
                    pthread_mutex_lock(&head_m);
                    node->next = pred->next;
                    head = node;
                    pthread_mutex_unlock(&head_m);
                }
                pred->next = node;
                update_counter(1);
                pthread_mutex_lock(&head_m);
                head = node;
                pthread_mutex_unlock(&head_m);
                __insert_test_hook();
                pthread_mutex_unlock(&pred->m);
                return true;
            }
            curr = pred->next;
            pthread_mutex_lock(&curr->m);
            while(curr->data <= data){
                if(curr->data == data){
                    pthread_mutex_unlock(&pred->m);
                    pthread_mutex_unlock(&curr->m);
                    return false;
                }
                if(curr->next == nullptr || curr->next->data > data){
                    pthread_mutex_unlock(&pred->m);
                    pred = curr;
                    curr = curr->next;
                    if (curr != nullptr){
                        pthread_mutex_lock(&curr->m);
                    }
                    Node* node = new Node(data);
                    pred->next = node;
                    update_counter(1);
                    node->next = curr;

                    if (curr != nullptr){
                        pthread_mutex_unlock(&curr->m);
                    }
                    pthread_mutex_unlock(&pred->m);
                    __insert_test_hook();
                    return true;
                }

                pthread_mutex_unlock(&pred->m);
                pred = curr;
                curr = curr->next;
                if(pred->next == nullptr){
                    pthread_mutex_unlock(&pred->m);
                    return false;
                }
                pthread_mutex_lock(&curr->m);
            }

            pthread_mutex_unlock(&curr->m);
            pthread_mutex_unlock(&pred->m);
            return false;
        }

        /**
         * Remove the node that its data equals to @param value
         * @param value the data to lookup a node that has the same data to be removed
         * @return true if a matched node was found and removed and false otherwise
         */
        bool remove(const T& value) {
            Node *pred, *curr;
            pred = &dummy;
            pthread_mutex_lock(&pred->m);
            if(pred->next == nullptr){
                pthread_mutex_unlock(&pred->m);
                return false;
            }
            curr = pred->next;
            pthread_mutex_lock(&curr->m);
            if(curr->data == value){
                pred->next = curr->next;
                pthread_mutex_lock(&head_m);
                head = curr->next;
                pthread_mutex_unlock(&head_m);
                pthread_mutex_unlock(&curr->m);
                delete curr;
                update_counter(-1);
                pthread_mutex_unlock(&pred->m);
                __remove_test_hook();
                return true;
            }

            while(curr->data <= value){
                if(curr->data == value){
                    pred->next = curr->next;
                    pthread_mutex_unlock(&curr->m);
                    delete curr;
                    update_counter(-1);
                    pthread_mutex_unlock(&pred->m);
                    __remove_test_hook();
                    return true;
                }

                pthread_mutex_unlock(&pred->m);
                pred = curr;
                curr = curr->next;
                if(curr == nullptr){
                    pthread_mutex_unlock(&pred->m);
                    return false;
                }
                pthread_mutex_lock(&curr->m);
            }

            pthread_mutex_unlock(&curr->m);
            pthread_mutex_unlock(&pred->m);
            return false;

        }

        /**
         * Returns the current size of the list
         * @return current size of the list
         */
        unsigned int getSize() {
            unsigned int i;
            pthread_mutex_lock(&counter_m);
            i = counter;
            pthread_mutex_unlock(&counter_m);
            return i;
        }

		// Don't remove
        void print() {
          Node* temp = head;
          if (temp == NULL)
          {
            cout << "";
          }
          else if (temp->next == NULL)
          {
            cout << temp->data;
          }
          else
          {
            while (temp != NULL)
            {
              cout << right << setw(3) << temp->data;
              temp = temp->next;
              cout << " ";
            }
          }
          cout << endl;
        }

		// Don't remove
        virtual void __insert_test_hook() {}
		// Don't remove
        virtual void __remove_test_hook() {}

private:
    Node* head;
    Node dummy;
        pthread_mutex_t head_m{};
        pthread_mutex_t counter_m{};
        int counter;

        void update_counter(int update) {
            pthread_mutex_lock(&counter_m);
            counter += update;
            pthread_mutex_unlock(&counter_m);
        }


};



#endif //THREAD_SAFE_LIST_H_